#include "nuvio/authsync/CollectionSyncController.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "nuvio/library/CollectionStore.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncIdentity.h"

namespace nuvio::authsync {

CollectionSyncController::CollectionSyncController(AuthConfig cfg,
                                                   TokenProvider token,
                                                   int profileId,
                                                   QObject* parent)
    : QObject(parent),
      m_cfg(std::move(cfg)),
      m_token(std::move(token)),
      m_client(new SyncRpcClient(m_cfg, [this] { return m_token(); }, this)),
      m_profileId(profileId)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(500);   // CollectionSyncService parity
    connect(&m_debounce, &QTimer::timeout, this,
            &CollectionSyncController::pushNow);
}

CollectionSyncController::~CollectionSyncController() = default;

QString CollectionSyncController::originId()
{
    nuvio::settings::PropertiesStore idStore(
        nuvio::settings::PropertiesStore::defaultPath(
            "sync_client_identity"));
    return nuvio::settings::SyncIdentity::currentClientId(idStore);
}

void CollectionSyncController::onLocalCollectionsChanged()
{
    if (!m_cfg.valid() || !signedIn() || m_applyRemote) return;
    if (!m_debounce.isActive()) m_debounce.start();
}

void CollectionSyncController::pushNow()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0 || m_applyRemote)
        return;
    nuvio::library::CollectionStore store(m_profileId);   // fresh disk view
    const QJsonDocument payload =
        QJsonDocument::fromJson(store.exportToJson().toUtf8());
    const QJsonArray arr = payload.isArray() ? payload.array() : QJsonArray{};
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con](bool ok, int, const QJsonDocument&,
                               QByteArray) {
                       disconnect(*con);
                       --m_inFlight;
                       emit pushFinished(ok);
                   });
    m_client->call(QString::fromLatin1("sync_push_collections"),
                   QJsonObject{
                       {QStringLiteral("p_profile_id"), m_profileId},
                       {QStringLiteral("p_collections_json"), arr},
                       {QStringLiteral("p_origin_client_id"), originId()},
                   });
}

void CollectionSyncController::pullNow()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) {
        emit pullFinished(false, false);
        return;
    }
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con](bool ok, int, const QJsonDocument& doc,
                               QByteArray) {
                       disconnect(*con);
                       --m_inFlight;
                       if (!ok) {
                           emit pullFinished(false, false);
                           return;
                       }
                       // Postgrest returns rows; the collections array may
                       // arrive bare or wrapped - accept both.
                       QString json;
                       if (doc.isArray()) {
                           const QJsonArray rows = doc.array();
                           const QJsonValue wrapped =
                               !rows.isEmpty()
                                   ? rows.first().toObject().value(
                                         QStringLiteral("collections"))
                                   : QJsonValue();
                           if (wrapped.isArray()) {
                               json = QString::fromUtf8(
                                   QJsonDocument(wrapped.toArray())
                                       .toJson(QJsonDocument::Compact));
                           } else if (wrapped.isObject()) {
                               json = QString::fromUtf8(
                                   QJsonDocument(wrapped.toObject())
                                       .toJson(QJsonDocument::Compact));
                           } else if (!rows.isEmpty() &&
                                      rows.first()
                                          .toObject()
                                          .contains(QLatin1String(
                                              "collections"))) {
                               json = QStringLiteral("[]");
                           } else {
                               json = QString::fromUtf8(
                                   doc.toJson(QJsonDocument::Compact));
                           }
                       } else {
                           json = QStringLiteral("[]");
                       }
                       m_applyRemote = true;
                       nuvio::library::CollectionStore store(m_profileId);
                       const QString before = store.exportToJson();
                       store.applyFromRemote(json);
                       m_applyRemote = false;
                       emit pullFinished(true,
                                         store.exportToJson() != before);
                   });
    m_client->call(QString::fromLatin1("sync_pull_collections"),
                   QJsonObject{{QStringLiteral("p_profile_id"), m_profileId}});
}

} // namespace nuvio::authsync
