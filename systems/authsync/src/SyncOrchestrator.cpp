#include "nuvio/authsync/SyncOrchestrator.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <memory>
#include <optional>

#include "nuvio/settings/AppSettings.h"
#include "nuvio/settings/SyncBlobFeatures.h"
#include "nuvio/settings/SyncIdentity.h"
#include "nuvio/settings/SyncPlayerSettings.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/debrid/DebridSettings.h"
#include "nuvio/watching/ContinueWatchingPrefs.h"
#include "nuvio/watching/WatchRecorder.h"

namespace nuvio::authsync {

namespace {
constexpr auto kPlatform = "desktop";

[[nodiscard]] QByteArray sigOf(const QJsonObject& obj)
{
    return QCryptographicHash::hash(
        QJsonDocument(obj).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256);
}
} // namespace

SyncOrchestrator::SyncOrchestrator(settings::AppSettings* settings,
                                   AuthConfig cfg, TokenProvider token,
                                   QObject* parent)
    : QObject(parent),
      m_settings(settings),
      m_cfg(std::move(cfg)),
      m_token(std::move(token)),
      m_client(new SyncRpcClient(m_cfg, [this] { return m_token(); }, this))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(800);
    connect(&m_debounce, &QTimer::timeout, this,
            &SyncOrchestrator::doPush);
}

SyncOrchestrator::~SyncOrchestrator() = default;

void SyncOrchestrator::pullNow()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) {
        m_pullAttempted = true;   // attempt concluded; pushes may proceed
        emit pullFinished(false);
        return;
    }
    const int profileId = m_profileId;
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(
        m_client, &SyncRpcClient::finished, this,
        [this, con, profileId](bool ok, int status,
                               const QJsonDocument& doc, QByteArray) {
            disconnect(*con);
            --m_inFlight;
            m_pullAttempted = true;
            if (!ok || status != 200) {
                emit pullFinished(false);
                return;
            }
            // postgrest RPC returns an ARRAY of rows; tolerate a bare
            // object shape too (older servers / manual endpoints).
            QJsonObject row;
            if (doc.isArray()) {
                const QJsonArray rows = doc.array();
                if (!rows.isEmpty()) row = rows.first().toObject();
            } else if (doc.isObject()) {
                row = doc.object();
            }
            const QJsonObject blob =
                row.value(QStringLiteral("settings_json")).toObject();
            const QJsonObject features =
                blob.value(QStringLiteral("features")).toObject();
            const QJsonObject player = features
                                           .value(QLatin1String(
                                               settings::BlobFeature::kPlayer))
                                           .toObject();

            // Passthrough first: cache every received unowned feature so
            // the NEXT push preserves sibling-client state (SyncBlobFeatures
            // rule). Merge-only - a partial pull never evicts.
            m_passthrough.mergeFromPull(features);

            bool applied = false;

            // CW payload string applies verbatim into the Compose-shared CW
            // store (unknown fields survive: our decode is tolerant and we
            // never re-encode on this path); the recorder reloads + notifies.
            const QJsonValue cwValue = features.value(QLatin1String(
                settings::BlobFeature::kContinueWatching));
            if (cwValue.isString()) {
                nuvio::watching::ContinueWatchingPrefsStore cwStore(profileId);
                if (cwStore.loadRaw() != cwValue.toString()) {
                    cwStore.saveRaw(cwValue.toString());
                    if (m_recorder) m_recorder->reloadContinueWatchingPrefs();
                    applied = true;
                }
            }

            if (!player.isEmpty()) {
                const QByteArray remoteSig = sigOf(player);
                if (remoteSig != sigOf(m_settings->exportPlayerSyncPayload())) {
                    m_applyRemote = true;   // suppress our own echo push
                    m_settings->applyPlayerSyncPayload(player);
                    m_applyRemote   = false;
                    applied         = true;
                }
            }

            // Debrid fragment applies through the settings object (owned
            // feature; per-key merge, absent keys untouched). Runs before
            // the nothing-owned gate below so player-less blobs still land.
            if (m_debrid) {
                const QJsonObject debrid = features
                                               .value(QLatin1String(
                                                   settings::BlobFeature::
                                                       kDebrid))
                                               .toObject();
                if (!debrid.isEmpty()) {
                    m_applyRemote = true;   // suppress our own echo push
                    const bool debridTouched =
                        m_debrid->applySyncPayload(debrid);
                    m_applyRemote   = false;
                    applied         = applied || debridTouched;
                }
            }

            if (player.isEmpty() && !applied) {
                emit pullFinished(false);   // nothing we own yet
                return;
            }

            if (!applied) {
                emit pullFinished(false);   // already identical
                return;
            }

            // Arm echo suppression against the FULL push-shape blob (fresh
            // player export + just-merged passthrough), not the player
            // fragment alone.
            m_applyRemote   = false;
            m_skipNextSig   = sigOf(fullPushBlob());
            emit pullFinished(true);
        });
    m_client->call(QString::fromLatin1(SyncFn::kPullProfileBlob),
                   QJsonObject{{QStringLiteral("p_profile_id"), profileId},
                               {QStringLiteral("p_platform"),
                                QLatin1String(kPlatform)}});
}

void SyncOrchestrator::beginObserving()
{
    // Uniform subscription: the push-payload dedup makes non-player signals
    // cheap no-ops unless the player fragment genuinely moved.
    connect(m_settings, &nuvio::settings::AppSettings::decoderModeChanged,
            this, &SyncOrchestrator::schedulePush);
    connect(m_settings, &nuvio::settings::AppSettings::cacheMbChanged,
            this, &SyncOrchestrator::schedulePush);
    connect(m_settings,
            &nuvio::settings::AppSettings::preferredAudioLanguageChanged,
            this, &SyncOrchestrator::schedulePush);
    connect(m_settings,
            &nuvio::settings::AppSettings::preferredSubtitleLanguageChanged,
            this, &SyncOrchestrator::schedulePush);
    connect(m_settings,
            &nuvio::settings::AppSettings::useForcedSubtitlesChanged,
            this, &SyncOrchestrator::schedulePush);
    connect(m_settings, &nuvio::settings::AppSettings::subtitleStyleChanged,
            this, &SyncOrchestrator::schedulePush);
    connect(m_settings, &nuvio::settings::AppSettings::streamAutoPlayChanged,
            this, &SyncOrchestrator::schedulePush);
    connect(m_settings, &nuvio::settings::AppSettings::playerOptionsChanged,
            this, &SyncOrchestrator::schedulePush);
    connect(m_settings, &nuvio::settings::AppSettings::darkThemeChanged,
            this, &SyncOrchestrator::schedulePush);   // dedup eats it
    connect(m_settings, &nuvio::settings::AppSettings::discordEnabledChanged,
            this, &SyncOrchestrator::schedulePush);   // dedup eats it
    connect(m_settings,
            &nuvio::settings::AppSettings::torrentCacheSizeChanged,
            this, &SyncOrchestrator::schedulePush);   // dedup eats it
}

QByteArray SyncOrchestrator::currentExportSig()
{
    return sigOf(fullPushBlob());
}

QJsonObject SyncOrchestrator::fullPushBlob()
{
    QJsonObject passthrough = m_passthrough.loadAll();
    // CW prefs ride the blob as the raw payload string (Compose parity):
    // re-sent verbatim from the shared store so local recorder edits
    // propagate; omitted while never set (never fabricate "").
    nuvio::watching::ContinueWatchingPrefsStore cwStore(m_profileId);
    const QString cwRaw = cwStore.loadRaw();
    if (!cwRaw.isEmpty())
        passthrough.insert(
            QLatin1String(settings::BlobFeature::kContinueWatching),
            cwRaw);
    QJsonObject player = m_settings->exportPlayerSyncPayload();
    if (m_debrid) {
        // Debrid is an OWNED feature now: fresh export joins the blob,
        // minus the credential keys (Compose credential-policy parity -
        // those sync through the provider-credentials family only).
        QJsonObject debrid = m_debrid->exportSyncPayload();
        for (const char* cred : {"debrid_torbox_api_key",
                                 "debrid_premiumize_api_key",
                                 "debrid_real_debrid_api_key"})
            debrid.remove(QLatin1String(cred));
        if (!debrid.isEmpty())
            passthrough.insert(
                QLatin1String(settings::BlobFeature::kDebrid), debrid);
    }
    return settings::buildPushBlob(player, passthrough);
}

QJsonObject SyncOrchestrator::baseParams()
{
    nuvio::settings::PropertiesStore idStore(
        nuvio::settings::PropertiesStore::defaultPath("sync_client_identity"));
    return QJsonObject{
        {QStringLiteral("p_profile_id"), m_profileId},
        {QStringLiteral("p_platform"), QLatin1String(kPlatform)},
        {QStringLiteral("p_origin_client_id"),
         nuvio::settings::SyncIdentity::currentClientId(idStore)},
    };
}

void SyncOrchestrator::schedulePush()
{
    if (m_inFlight != 0 || m_applyRemote || !m_pullAttempted) return;
    if (!m_debounce.isActive()) m_debounce.start();
}

void SyncOrchestrator::doPush()
{
    // First-push-before-pull gate: without a pull attempt the passthrough
    // cache is cold and a push could drop server-side sibling features
    // (SyncBlobFeatures rule). The startup pullNow() always runs first.
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0 || m_applyRemote
        || !m_pullAttempted)
        return;

    const QJsonObject blob = fullPushBlob();
    const QByteArray payloadSig = sigOf(blob);

    // Echo suppression #1: nothing visibly changed since the last merge.
    if (m_skipNextSig && payloadSig == *m_skipNextSig) {
        m_skipNextSig.reset();
        return;
    }
    // Echo suppression #2: identical to what the server already holds.
    if (m_lastPushSig && payloadSig == *m_lastPushSig) return;

    QJsonObject params = baseParams();
    params.insert(QStringLiteral("p_settings_json"), blob);

    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(
        m_client, &SyncRpcClient::finished, this,
        [this, con, payloadSig](bool ok, int, const QJsonDocument&,
                                QByteArray) {
            disconnect(*con);
            --m_inFlight;
            if (ok) m_lastPushSig = payloadSig;
            emit pushFinished(ok);
        });
    m_client->call(QString::fromLatin1(SyncFn::kPushProfileBlob), params);
}

} // namespace nuvio::authsync
