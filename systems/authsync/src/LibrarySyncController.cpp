#include "nuvio/authsync/LibrarySyncController.h"

#include <algorithm>

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cstdio>

#include "nuvio/library/LibrarySyncCodec.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncIdentity.h"

namespace nuvio::authsync {

namespace {
constexpr int kSnapshotPageSize = 500;   // LibrarySyncPaging parity
constexpr int kDeltaPageSize = 500;
constexpr int kMutationBatchSize = 500;

[[nodiscard]] QString itemKey(const nuvio::library::LibraryItem& it)
{
    return nuvio::library::libraryItemKey(it.id, it.type);
}
[[nodiscard]] QString syncKeyStr(const nuvio::library::LibrarySyncKey& k)
{
    return nuvio::library::libraryItemKey(k.contentId, k.contentType);
}
} // namespace

LibrarySyncController::LibrarySyncController(AuthConfig cfg,
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
            &LibrarySyncController::pushDirty);
}

LibrarySyncController::~LibrarySyncController() = default;

QString LibrarySyncController::originId()
{
    nuvio::settings::PropertiesStore idStore(
        nuvio::settings::PropertiesStore::defaultPath(
            "sync_client_identity"));
    return nuvio::settings::SyncIdentity::currentClientId(idStore);
}

void LibrarySyncController::onLocalLibraryChanged()
{
    if (!m_cfg.valid() || !signedIn()) return;
    if (!m_debounce.isActive()) m_debounce.start();
}

void LibrarySyncController::pushDirty()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) return;
    nuvio::library::LibraryStore store(m_profileId);   // fresh disk view
    const auto upserts = store.pendingUpserts();
    const auto deletes = store.pendingDeletes();
    if (upserts.isEmpty() && deletes.isEmpty()) {
        emit pushFinished(true, 0);
        return;
    }

    // Resolve dirty upserts to current items (a row deleted after dirtying
    // falls out of the push and keeps its tombstone instead).
    QHash<QString, nuvio::library::LibraryItem> byKey;
    for (const auto& it : store.items()) byKey.insert(itemKey(it), it);
    QList<QJsonObject> pushItems;
    QList<nuvio::library::LibrarySyncKey> pushKeys;
    for (const auto& k : upserts) {
        const auto it = byKey.find(syncKeyStr(k));
        if (it == byKey.end()) continue;
        pushItems.append(
            nuvio::library::LibrarySyncCodec::toSyncItem(*it));
        pushKeys.append(k);
    }

    const QString origin = originId();
    ++m_inFlight;

    // Upsert leg first, then the delete leg; each clears only its own
    // acknowledged keys. Over-500 dirty sets send the first batch and
    // leave the rest dirty (self-healing on the next trigger; libraries
    // that large never fit one Compose batch either).
    auto runDeletes = [this, deletes, origin] {
        if (deletes.isEmpty()) {
            --m_inFlight;
            emit pushFinished(true, 0);
            return;
        }
        auto con = std::make_shared<QMetaObject::Connection>();
        *con = connect(m_client, &SyncRpcClient::finished, this,
                       [this, con, deletes](bool ok, int,
                                            const QJsonDocument&,
                                            QByteArray) {
                           disconnect(*con);
                           --m_inFlight;
                           if (ok) {
                               nuvio::library::LibraryStore fresh(
                                   m_profileId);
                               fresh.clearPendingDeletes(deletes);
                           }
                           emit pushFinished(ok, int(deletes.size()));
                       });
        int from = 0;
        Q_UNUSED(from);
        // Compose sends one call per 500-batch; small libraries fit in one.
        QList<nuvio::library::LibrarySyncKey> batch =
            deletes.mid(from, kMutationBatchSize);
        m_client->call(QString::fromLatin1("sync_delete_library_items"),
                       nuvio::library::LibrarySyncCodec::deleteItemsParams(
                           m_profileId, batch, origin));
    };

    if (pushItems.isEmpty()) {
        runDeletes();
        return;
    }
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, pushKeys, runDeletes](bool ok, int,
                                                     const QJsonDocument&,
                                                     QByteArray) mutable {
                       disconnect(*con);
                       if (ok) {
                           nuvio::library::LibraryStore fresh(m_profileId);
                           fresh.clearPendingUpserts(pushKeys);
                       }
                       if (!ok) {
                           --m_inFlight;
                           emit pushFinished(false, 0);
                           return;
                       }
                       runDeletes();
                   });
    QList<QJsonObject> batch =
        pushItems.mid(0, kMutationBatchSize);
    m_client->call(QString::fromLatin1("sync_push_library_items"),
                   nuvio::library::LibrarySyncCodec::pushItemsParams(
                       m_profileId, batch, origin));
}

void LibrarySyncController::fullLibrarySyncThenDeltas()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) return;
    m_snapAccum.clear();
    fetchSnapshotPage(0);
}

void LibrarySyncController::fetchSnapshotPage(int offset)
{
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, offset](bool ok, int,
                                       const QJsonDocument& doc, QByteArray) {
                       disconnect(*con);
                       --m_inFlight;
                       if (!ok) {
                           emit pullFinished(false, 0);
                           return;
                       }
                       const QJsonArray arr =
                           doc.isArray() ? doc.array() : QJsonArray{};
                       for (const QJsonValue& v : arr)
                           m_snapAccum.push_back(
                               nuvio::library::LibrarySyncCodec::fromSyncItem(
                                   v.toObject()));
                       if (int(arr.size()) >= kSnapshotPageSize) {
                           fetchSnapshotPage(offset + kSnapshotPageSize);
                           return;
                       }
                       finishSnapshotMerge();
                   });
    m_client->call(QString::fromLatin1("sync_pull_library"),
                   QJsonObject{
                       {QStringLiteral("p_profile_id"), m_profileId},
                       {QStringLiteral("p_limit"), kSnapshotPageSize},
                       {QStringLiteral("p_offset"), offset},
                   });
}

void LibrarySyncController::finishSnapshotMerge()
{
    nuvio::library::LibraryStore store(m_profileId);
    QHash<QString, nuvio::library::LibraryItem> server;
    for (const auto& it : m_snapAccum) server.insert(itemKey(it), it);
    QHash<QString, nuvio::library::LibraryItem> local;
    for (const auto& it : store.items()) local.insert(itemKey(it), it);

    const auto pendUp = store.pendingUpserts();
    const auto pendDel = store.pendingDeletes();
    QSet<QString> upSet, delSet;
    for (const auto& k : pendUp) upSet.insert(syncKeyStr(k));
    for (const auto& k : pendDel) delSet.insert(syncKeyStr(k));

    // Legacy migration (reconciler parity): empty server + dirty-free
    // local library keeps local rows by marking every one dirty (they
    // push on the next dirty cycle); nothing is replaced.
    const bool migrate = server.isEmpty() && !local.isEmpty() &&
                         pendUp.isEmpty() && pendDel.isEmpty();
    if (migrate) {
        for (const auto& it : store.items())
            store.addToLibrary(it.type, it.id, it.name, it.poster,
                               it.description, it.savedAtEpochMs);
    } else {
        QList<nuvio::library::LibraryItem> merged;
        for (const QString& dk : delSet) server.remove(dk);
        for (const QString& uk : upSet) {
            const auto lit = local.find(uk);
            if (lit != local.end()) server.insert(uk, *lit);
        }
        merged = server.values();
        store.replaceItems(merged);
    }

    // Cursor seed (bare-Long RPC, watched-cursor precedent).
    auto cursorCon = std::make_shared<QMetaObject::Connection>();
    *cursorCon = connect(
        m_client, &SyncRpcClient::finished, this,
        [this, cursorCon](bool ok, int, const QJsonDocument&,
                          QByteArray raw) {
            disconnect(*cursorCon);
            if (ok) {
                nuvio::library::LibraryStore fresh(m_profileId);
                fresh.setDeltaCursor(
                    nuvio::library::LibrarySyncCodec::parseCursor(raw, 0),
                    true);
            }
            m_initialSyncDone = true;
            emit pullFinished(ok, 0);
            if (ok) pullLibraryDelta();
        });
    m_client->call(QString::fromLatin1("sync_get_library_delta_cursor"),
                   QJsonObject{{QStringLiteral("p_profile_id"), m_profileId}});
}

void LibrarySyncController::pullLibraryDelta()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) return;
    nuvio::library::LibraryStore peek(m_profileId);
    if (!peek.deltaInitialized()) return;   // snapshot leg seeds first
    const qint64 since = peek.deltaCursorEventId();
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con](bool ok, int, const QJsonDocument&,
                               QByteArray raw) {
                       disconnect(*con);
                       --m_inFlight;
                       if (!ok) {
                           emit pullFinished(false, 0);
                           return;
                       }
                       const auto events = nuvio::library::LibrarySyncCodec::
                           parseDeltaEvents(raw);
                       if (events.isEmpty()) {
                           emit pullFinished(true, 0);
                           return;
                       }
                       nuvio::library::LibraryStore store(m_profileId);
                       QSet<QString> upSet, delSet;
                       for (const auto& k : store.pendingUpserts())
                           upSet.insert(syncKeyStr(k));
                       for (const auto& k : store.pendingDeletes())
                           delSet.insert(syncKeyStr(k));
                       // Event order by id (reconciler sorts by eventId).
                       QList<nuvio::library::LibraryDeltaEvent> sorted =
                           events;
                       std::sort(sorted.begin(), sorted.end(),
                                 [](const auto& a, const auto& b) {
                                     return a.eventId < b.eventId;
                                 });
                       QList<nuvio::library::LibraryItem> upserts;
                       QList<nuvio::library::LibrarySyncKey> deletes;
                       qint64 cursor = store.deltaCursorEventId();
                       for (const auto& e : sorted) {
                           cursor = std::max(cursor, e.eventId);
                           const auto item = nuvio::library::LibrarySyncCodec::
                               fromSyncItem(e.item);
                           const QString key = itemKey(item);
                           if (upSet.contains(key) ||
                               delSet.contains(key))
                               continue;   // pending keys win locally
                           if (e.operation.trimmed().toLower() ==
                               QStringLiteral("delete")) {
                               nuvio::library::LibrarySyncKey k{
                                   item.id, item.type};
                               deletes.append(k);
                           } else if (e.operation.trimmed().toLower() ==
                                      QStringLiteral("upsert")) {
                               upserts.append(item);
                           }
                       }
                       store.upsertRemoteItems(upserts);
                       store.removeRemoteKeys(deletes);
                       store.setDeltaCursor(cursor, true);
                       emit pullFinished(true, int(sorted.size()));
                   });
    m_client->call(QString::fromLatin1("sync_pull_library_delta"),
                   QJsonObject{
                       {QStringLiteral("p_profile_id"), m_profileId},
                       {QStringLiteral("p_since_event_id"), since},
                       {QStringLiteral("p_limit"), kDeltaPageSize},
                   });
}

} // namespace nuvio::authsync
