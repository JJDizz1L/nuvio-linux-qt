#include "nuvio/authsync/ProgressSyncController.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <memory>

#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncIdentity.h"
#include "nuvio/watching/ProgressSyncCodec.h"
#include "nuvio/watching/WatchingStore.h"

namespace nuvio::authsync {

ProgressSyncController::ProgressSyncController(AuthConfig cfg,
                                               TokenProvider token,
                                               int profileId, QObject* parent)
    : QObject(parent),
      m_cfg(std::move(cfg)),
      m_token(std::move(token)),
      m_client(new SyncRpcClient(m_cfg, [this] { return m_token(); }, this)),
      m_profileId(profileId)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(1500);
    connect(&m_debounce, &QTimer::timeout, this,
            &ProgressSyncController::pushDirty);
    m_watchedDebounce.setSingleShot(true);
    m_watchedDebounce.setInterval(1500);
    connect(&m_watchedDebounce, &QTimer::timeout, this,
            &ProgressSyncController::pushWatchedDirty);
}

ProgressSyncController::~ProgressSyncController() = default;

QString ProgressSyncController::originId()
{
    nuvio::settings::PropertiesStore idStore(
        nuvio::settings::PropertiesStore::defaultPath("sync_client_identity"));
    return nuvio::settings::SyncIdentity::currentClientId(idStore);
}

void ProgressSyncController::onLocalProgressChanged()
{
    if (!m_cfg.valid() || !signedIn()) return;
    if (!m_debounce.isActive()) m_debounce.start();
}

void ProgressSyncController::pushDirty()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) return;

    nuvio::watching::WatchingStore store(m_profileId);   // fresh disk view
    const auto dirty = store.loadProgressEnvelope().dirtyProgressKeys;
    if (dirty.empty()) return;

    const auto entries = store.loadEntries();
    std::vector<nuvio::watching::WatchEntry> toPush;
    for (const auto& key : dirty) {
        for (const auto& e : entries)
            if (e.resolvedProgressKey() == key) {
                toPush.push_back(e);
                break;
            }
    }
    // Absent dirty keys = local deletions, handled by the delete leg.
    m_pendingDeletes.clear();
    for (const auto& key : dirty) {
        const bool present =
            std::any_of(toPush.begin(), toPush.end(),
                        [&](const auto& e) {
                            return e.resolvedProgressKey() == key;
                        });
        if (!present) m_pendingDeletes.push_back(key);
    }
    if (toPush.empty() && m_pendingDeletes.empty()) {
        store.clearProgressDirty(dirty);
        return;
    }

    const QString origin = originId();
    ++m_inFlight;

    if (!toPush.empty()) {
        auto con = std::make_shared<QMetaObject::Connection>();
        *con = connect(m_client, &SyncRpcClient::finished, this,
                       [this, con, dirty, toDelete = m_pendingDeletes,
                        origin](bool ok, int, const QJsonDocument&,
                                QByteArray) {
            disconnect(*con);
            if (!ok) {
                m_inFlight = 0;
                emit pushFinished(false, 0);
                return;
            }
            if (!toDelete.empty()) {
                m_pendingDeletes = toDelete;
                runDeleteLeg(toDelete);   // chains its own finalize
                return;
            }
            {
                nuvio::watching::WatchingStore fresh(m_profileId);
                fresh.clearProgressDirty(dirty);
                fresh.setLastSuccessfulPush(
                    QDateTime::currentMSecsSinceEpoch());
            }
            m_inFlight = 0;
            emit pushFinished(true, 1);
        });
        m_client->call(QStringLiteral("sync_push_watch_progress"),
                       nuvio::watching::ProgressSyncCodec::pushParams(
                           m_profileId, toPush, origin));
        return;
    }

    // Deletes only.
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, dirty](bool ok, int, const QJsonDocument&,
                                      QByteArray) {
        disconnect(*con);
        m_inFlight = 0;
        if (ok) {
            nuvio::watching::WatchingStore fresh(m_profileId);
            fresh.clearProgressDirty(dirty);
            fresh.setLastSuccessfulPush(QDateTime::currentMSecsSinceEpoch());
        }
        emit pushFinished(ok, 0);
    });
    m_client->call(QStringLiteral("sync_delete_watch_progress"),
                   nuvio::watching::ProgressSyncCodec::deleteParams(
                       m_profileId, m_pendingDeletes, origin));
    m_pendingDeletes.clear();
}

void ProgressSyncController::fullSyncThenDeltas()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) {
        emit pullFinished(false, 0);
        return;
    }

    nuvio::watching::WatchingStore store(m_profileId);
    const auto env = store.loadProgressEnvelope();
    const bool initial = !env.deltaInitialized;

    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, initial](bool ok, int status,
                                        const QJsonDocument& doc,
                                        QByteArray raw) {
        disconnect(*con);

        if (!ok || status != 200) {
            --m_inFlight;
            emit pullFinished(false, 0);
            return;
        }

        // ---------- INITIAL: full snapshot merge, then seed cursor ----------
        if (initial) {
            nuvio::watching::WatchingStore fresh(m_profileId);
            int applied = 0;
            for (const auto& rec :
                 nuvio::watching::ProgressSyncCodec::decodeRecords(doc)) {
                const auto local = fresh.loadEntries();
                bool newer = true;
                for (const auto& l : local)
                    if (l.resolvedProgressKey() ==
                        rec.resolvedProgressKey()) {
                        newer = rec.lastUpdatedEpochMs >
                                l.lastUpdatedEpochMs;
                        break;
                    }
                if (newer) { fresh.upsertRemote(rec); ++applied; }
            }

            // Chain the cursor seed while HOLDING the in-flight slot so no
            // push can interleave between merge and cursor initialization.
            auto ccon = std::make_shared<QMetaObject::Connection>();
            *ccon = connect(m_client, &SyncRpcClient::finished, this,
                            [this, ccon, applied](bool ok2, int st2,
                                    const QJsonDocument&, QByteArray raw2) {
                disconnect(*ccon);
                --m_inFlight;
                if (ok2 && st2 == 200) {
                    if (const auto c = nuvio::watching::
                            ProgressSyncCodec::parseCursor(raw2)) {
                        nuvio::watching::WatchingStore f(m_profileId);
                        f.setDeltaCursor(*c, true);
                    }
                }
                // deltaInitialized stays false on cursor failure -> next
                // fullSync retries the (safe) full path.
                emit pullFinished(ok2, applied);
            });
            m_client->call(
                QStringLiteral("sync_get_watch_progress_delta_cursor"),
                nuvio::watching::ProgressSyncCodec::cursorParams(
                    m_profileId));
            return;
        }

        // ---------- DELTA loop ----------------------------------------------
        --m_inFlight;
        nuvio::watching::WatchingStore fresh(m_profileId);
        int applied = 0;
        long long maxEvent = 0;
        for (const auto& d :
             nuvio::watching::ProgressSyncCodec::decodeDeltas(doc)) {
            maxEvent = std::max(maxEvent, d.eventId);
            if (d.operation == "delete") {
                fresh.remove(d.progressKey);
                ++applied;
                continue;
            }
            nuvio::watching::WatchEntry e;
            e.parentMetaId = d.contentId;
            e.contentType  = d.contentType;
            e.videoId      = d.videoId;
            e.season       = d.season;
            e.episode      = d.episode;
            e.lastPositionMs     = d.position;
            e.durationMs         = d.duration;
            e.lastUpdatedEpochMs = d.lastWatched;
            e.progressKey        = d.progressKey;

            const auto local = fresh.loadEntries();
            bool newer = true;
            for (const auto& l : local)
                if (l.resolvedProgressKey() == e.resolvedProgressKey()) {
                    newer = d.lastWatched > l.lastUpdatedEpochMs;
                    break;
                }
            if (newer) { fresh.upsertRemote(e); ++applied; }
        }
        if (maxEvent > 0) fresh.setDeltaCursor(maxEvent, true);
        emit pullFinished(ok, applied);
    });

    if (initial)
        m_client->call(QStringLiteral("sync_pull_watch_progress"),
                       nuvio::watching::ProgressSyncCodec::fullPullParams(
                           m_profileId));
    else
        m_client->call(QStringLiteral("sync_pull_watch_progress_delta"),
                       nuvio::watching::ProgressSyncCodec::deltaPullParams(
                           m_profileId, env.deltaCursorEventId, 200));
}

void ProgressSyncController::runDeleteLeg(
    const std::vector<std::string>& keys)
{
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, keys](bool ok, int, const QJsonDocument&,
                                     QByteArray) {
        disconnect(*con);
        m_inFlight = 0;
        if (ok) {
            nuvio::watching::WatchingStore fresh(m_profileId);
            fresh.clearProgressDirty(keys);
            fresh.setLastSuccessfulPush(QDateTime::currentMSecsSinceEpoch());
        }
        emit pushFinished(ok, 0);
    });
    m_client->call(QStringLiteral("sync_delete_watch_progress"),
                   nuvio::watching::ProgressSyncCodec::deleteParams(
                       m_profileId, keys, originId()));
    m_pendingDeletes.clear();
}

void ProgressSyncController::onWatchedChanged()
{
    if (!m_cfg.valid() || !signedIn()) return;
    if (!m_watchedDebounce.isActive()) m_watchedDebounce.start();
}

void ProgressSyncController::pushWatchedDirty()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) return;

    nuvio::watching::WatchingStore store(m_profileId);
    const auto dirty = store.dirtyWatchedKeys();
    if (dirty.empty()) return;

    const auto items = store.loadWatchedItems();
    std::vector<nuvio::watching::WatchedItem> toPush;
    std::vector<nuvio::watching::WatchedItem> toDelete;
    for (const auto& key : dirty) {
        bool found = false;
        for (const auto& w : items)
            if (nuvio::watching::buildWatchedKey(
                    w.type, w.id, w.season, w.episode) == key) {
                toPush.push_back(w);
                found = true;
                break;
            }
        if (found) continue;
        // Absent = unmarked locally -> delete by composite key
        // "type:id:s:e" (id may contain colons; s/e = last two tokens).
        const QList<QByteArray> parts = QByteArray(key.c_str()).split(':');
        if (parts.size() < 4) continue;
        nuvio::watching::WatchedItem w;
        w.type    = parts.first().toStdString();
        w.season  = parts[parts.size() - 2].toInt();
        w.episode = parts[parts.size() - 1].toInt();
        QByteArray mid;
        for (int i = 1; i + 2 < parts.size(); ++i) {
            if (!mid.isEmpty()) mid += ':';
            mid += parts[i];
        }
        w.id = mid.toStdString();
        toDelete.push_back(std::move(w));
    }
    if (toPush.empty() && toDelete.empty()) {
        store.clearWatchedDirtyKeys(dirty);
        return;
    }

    const QString origin = originId();
    ++m_inFlight;

    auto finalize = [this, dirty](bool ok) {
        if (ok) {
            nuvio::watching::WatchingStore fresh(m_profileId);
            fresh.clearWatchedDirtyKeys(dirty);
        }
        m_inFlight = std::max(m_inFlight - 1, 0);
        emit pushFinished(ok, 0);
    };

    if (!toPush.empty()) {
        auto con = std::make_shared<QMetaObject::Connection>();
        *con = connect(m_client, &SyncRpcClient::finished, this,
                       [this, con, finalize, toDelete, origin](bool ok, int,
                               const QJsonDocument&, QByteArray) {
            disconnect(*con);
            if (!ok) { finalize(false); return; }
            if (!toDelete.empty()) {
                auto dcon = std::make_shared<QMetaObject::Connection>();
                *dcon = connect(
                    m_client, &SyncRpcClient::finished, this,
                    [this, dcon, finalize, toDelete](bool ok2, int,
                            const QJsonDocument&, QByteArray) {
                        disconnect(*dcon);
                        finalize(ok2);
                    });
                m_client->call(
                    QStringLiteral("sync_delete_watched_items"),
                    nuvio::watching::ProgressSyncCodec::
                        watchedDeleteParams(m_profileId, toDelete, origin));
                return;
            }
            finalize(true);
        });
        m_client->call(QStringLiteral("sync_push_watched_items"),
                       nuvio::watching::ProgressSyncCodec::watchedPushParams(
                           m_profileId, toPush, origin));
        return;
    }

    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, finalize, toDelete](bool ok, int,
                           const QJsonDocument&, QByteArray) {
        disconnect(*con);
        finalize(ok);
    });
    m_client->call(QStringLiteral("sync_delete_watched_items"),
                   nuvio::watching::ProgressSyncCodec::watchedDeleteParams(
                       m_profileId, toDelete, origin));
}

void ProgressSyncController::fullWatchedSyncThenDeltas()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) {
        emit pullFinished(false, 0);
        return;
    }

    nuvio::watching::WatchingStore store(m_profileId);
    const long long watchedCursor = store.watchedDeltaCursorEventId();
    const bool watchedInit = store.watchedDeltaInitialized();

    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, watchedInit](bool ok, int status,
                                            const QJsonDocument& doc,
                                            QByteArray) {
        disconnect(*con);
        --m_inFlight;
        int applied = 0;
        if (ok && status == 200) {
            nuvio::watching::WatchingStore fresh(m_profileId);
            if (!watchedInit) {
                for (const auto& w :
                     nuvio::watching::ProgressSyncCodec::
                         decodeWatchedRecords(doc)) {
                    const auto items = fresh.loadWatchedItems();
                    bool newer = true;
                    for (const auto& l : items)
                        if (nuvio::watching::buildWatchedKey(
                                l.type, l.id, l.season, l.episode) ==
                            nuvio::watching::buildWatchedKey(
                                w.type, w.id, w.season, w.episode)) {
                            newer = w.markedAtEpochMs > l.markedAtEpochMs;
                            break;
                        }
                    if (newer) { fresh.upsertWatchedRemote(w); ++applied; }
                }
            } else {
                long long maxEvent = 0;
                for (const auto& d :
                     nuvio::watching::ProgressSyncCodec::
                         decodeWatchedDeltas(doc)) {
                    maxEvent = std::max(maxEvent, d.eventId);
                    if (d.operation == "delete") {
                        fresh.removeWatchedByContentId(d.contentId, d.season,
                                                       d.episode);
                        ++applied;
                        continue;
                    }
                    nuvio::watching::WatchedItem w;
                    w.id     = d.contentId;
                    w.type   = d.contentType;
                    w.name   = d.title;
                    w.season = d.season;
                    w.episode = d.episode;
                    w.markedAtEpochMs = d.watchedAt;
                    const auto items = fresh.loadWatchedItems();
                    bool newer = true;
                    for (const auto& l : items)
                        if (nuvio::watching::buildWatchedKey(
                                l.type, l.id, l.season, l.episode) ==
                            nuvio::watching::buildWatchedKey(
                                w.type, w.id, w.season, w.episode)) {
                            newer = d.watchedAt > l.markedAtEpochMs;
                            break;
                        }
                    if (newer) { fresh.upsertWatchedRemote(w); ++applied; }
                }
                if (maxEvent > 0) fresh.setWatchedCursor(maxEvent, true);
            }
        }
        emit pullFinished(ok, applied);
    });

    if (!watchedInit)
        m_client->call(QStringLiteral("sync_pull_watched_items"),
                       nuvio::watching::ProgressSyncCodec::
                           watchedPagePullParams(m_profileId, 1, 200));
    else
        m_client->call(QStringLiteral("sync_pull_watched_items_delta"),
                       nuvio::watching::ProgressSyncCodec::deltaPullParams(
                           m_profileId, watchedCursor, 200));
}

} // namespace nuvio::authsync