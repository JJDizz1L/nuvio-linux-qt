#include "nuvio/authsync/SyncOrchestrator.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <memory>
#include <optional>

#include "nuvio/settings/AppSettings.h"
#include "nuvio/settings/SyncIdentity.h"
#include "nuvio/settings/SyncPlayerSettings.h"
#include "nuvio/settings/PropertiesStore.h"

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
            const QJsonObject player =
                blob.value(QStringLiteral("features"))
                    .toObject()
                    .value(QStringLiteral("player_settings"))
                    .toObject();

            if (player.isEmpty()) {
                emit pullFinished(false);   // no remote player fragment yet
                return;
            }

            const QByteArray remoteSig = sigOf(player);
            if (remoteSig == currentExportSig()) {
                emit pullFinished(false);   // already identical
                return;
            }

            m_applyRemote = true;           // suppress our own echo push
            m_settings->applyPlayerSyncPayload(player);
            m_applyRemote   = false;
            m_skipNextSig   = sigOf(m_settings->exportPlayerSyncPayload());
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
    return sigOf(m_settings->exportPlayerSyncPayload());
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
    if (m_inFlight != 0 || m_applyRemote) return;
    if (!m_debounce.isActive()) m_debounce.start();
}

void SyncOrchestrator::doPush()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0 || m_applyRemote)
        return;

    const QJsonObject payload = m_settings->exportPlayerSyncPayload();
    const QByteArray payloadSig = sigOf(payload);

    // Echo suppression #1: nothing visibly changed since the last merge.
    if (m_skipNextSig && payloadSig == *m_skipNextSig) {
        m_skipNextSig.reset();
        return;
    }
    // Echo suppression #2: identical to what the server already holds.
    if (m_lastPushSig && payloadSig == *m_lastPushSig) return;

    const QJsonObject blob{
        {QStringLiteral("version"), 3},
        {QStringLiteral("features"),
         QJsonObject{{QStringLiteral("player_settings"), payload}}}};

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
