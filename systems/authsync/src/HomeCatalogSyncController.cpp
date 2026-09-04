#include "nuvio/authsync/HomeCatalogSyncController.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "nuvio/library/HomeCatalogSync.h"
#include "nuvio/library/HomeShelves.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncIdentity.h"

namespace nuvio::authsync {

HomeCatalogSyncController::HomeCatalogSyncController(
    AuthConfig cfg, TokenProvider token, UserIdProvider userId,
    library::HomeShelves* shelves, int profileId, QObject* parent)
    : QObject(parent),
      m_cfg(std::move(cfg)),
      m_token(std::move(token)),
      m_userId(std::move(userId)),
      m_shelves(shelves),
      m_client(new SyncRpcClient(m_cfg, [this] { return m_token(); }, this)),
      m_profileId(profileId)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(500);   // HomeCatalogSettingsSyncService parity
    connect(&m_debounce, &QTimer::timeout, this,
            &HomeCatalogSyncController::pushNow);
}

HomeCatalogSyncController::~HomeCatalogSyncController() = default;

QString HomeCatalogSyncController::originId()
{
    nuvio::settings::PropertiesStore idStore(
        nuvio::settings::PropertiesStore::defaultPath(
            "sync_client_identity"));
    return nuvio::settings::SyncIdentity::currentClientId(idStore);
}

void HomeCatalogSyncController::onLocalCatalogChanged()
{
    if (!m_cfg.valid() || !signedIn() || m_applyRemote) return;
    if (!pullTokenReady()) return;   // gated on the initial pull
    if (!m_debounce.isActive()) m_debounce.start();
}

void HomeCatalogSyncController::pushNow()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0 || m_applyRemote)
        return;
    if (!pullTokenReady() || !m_shelves) return;
    const QJsonObject local =
        m_shelves->exportSyncPayload().toJson();
    const QJsonObject merged =
        m_haveCachedRemote
            ? nuvio::library::mergeSyncJson(m_cachedRemote, local)
            : local;
    const QString user = m_userId();
    const int profile = m_profileId;
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, merged, user, profile](
                       bool ok, int, const QJsonDocument&, QByteArray) {
                       disconnect(*con);
                       --m_inFlight;
                       if (ok && user == m_userId() &&
                           profile == m_profileId) {
                           m_cachedRemote = merged;
                           m_haveCachedRemote = true;
                       }
                       emit pushFinished(ok);
                   });
    m_client->call(QString::fromLatin1(
                       nuvio::library::kHomeCatalogPushRpc),
                   QJsonObject{
                       {QStringLiteral("p_profile_id"), m_profileId},
                       {QStringLiteral("p_platform"),
                        QString::fromLatin1(
                            nuvio::library::kHomeCatalogSyncPlatform)},
                       {QStringLiteral("p_settings_json"), merged},
                       {QStringLiteral("p_origin_client_id"), originId()},
                   });
}

void HomeCatalogSyncController::pullNow()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0 || !m_shelves) {
        emit pullFinished(false, false);
        return;
    }
    const QString user = m_userId();
    const int profile = m_profileId;
    if (user.isEmpty()) {
        emit pullFinished(false, false);
        return;
    }
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, user, profile](bool ok, int,
                                              const QJsonDocument& doc,
                                              QByteArray) {
                       disconnect(*con);
                       --m_inFlight;
                       auto markComplete = [&] {
                           m_pullCompleteUser = user;
                           m_pullCompleteProfile = profile;
                       };
                       if (!ok) {
                           emit pullFinished(false, false);
                           return;
                       }
                       // Row array; the first row carries settings_json.
                       // Absent/non-object settings_json = no remote
                       // settings: preserve local, still complete the
                       // initial pull (service parity).
                       QJsonObject remote;
                       bool haveRemote = false;
                       if (doc.isArray() && !doc.array().isEmpty()) {
                           const QJsonValue settings =
                               doc.array()
                                   .first()
                                   .toObject()
                                   .value(QLatin1String(
                                       nuvio::library::
                                           kHomeCatalogSettingsJsonKey));
                           if (settings.isObject()) {
                               remote = settings.toObject();
                               haveRemote = true;
                           }
                       }
                       m_cachedRemote = remote;
                       m_haveCachedRemote = true;
                       markComplete();
                       if (!haveRemote) {
                           emit pullFinished(true, false);
                           return;
                       }
                       const nuvio::library::SyncHomeCatalogPayload local =
                           m_shelves->exportSyncPayload();
                       const nuvio::library::SyncHomeCatalogPayload decoded =
                           nuvio::library::SyncHomeCatalogPayload::fromJson(
                               remote, local.showCatalogType,
                               local.hideUnreleasedContent);
                       m_applyRemote = true;
                       const bool changed =
                           m_shelves->applySyncedPayload(decoded);
                       m_applyRemote = false;
                       emit pullFinished(true, changed);
                   });
    m_client->call(QString::fromLatin1(
                       nuvio::library::kHomeCatalogPullRpc),
                   QJsonObject{
                       {QStringLiteral("p_profile_id"), m_profileId},
                       {QStringLiteral("p_platform"),
                        QString::fromLatin1(
                            nuvio::library::kHomeCatalogSyncPlatform)},
                   });
}

} // namespace nuvio::authsync
