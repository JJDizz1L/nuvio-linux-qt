#include "nuvio/authsync/AddonsSyncController.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include <memory>

#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncIdentity.h"

namespace nuvio::authsync {

AddonsSyncController::AddonsSyncController(nuvio::library::AddonRegistry* registry,
                                           AuthConfig cfg, TokenProvider token,
                                           QObject* parent)
    : QObject(parent),
      m_registry(registry),
      m_cfg(std::move(cfg)),
      m_token(std::move(token)),
      m_client(new SyncRpcClient(m_cfg, [this] { return m_token(); }, this))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(800);
    connect(&m_debounce, &QTimer::timeout, this,
            &AddonsSyncController::doPush);
}

AddonsSyncController::~AddonsSyncController() = default;

QString AddonsSyncController::originId()
{
    nuvio::settings::PropertiesStore idStore(
        nuvio::settings::PropertiesStore::defaultPath("sync_client_identity"));
    return nuvio::settings::SyncIdentity::currentClientId(idStore);
}

void AddonsSyncController::beginObserving()
{
    connect(m_registry, &nuvio::library::AddonRegistry::changed, this,
            &AddonsSyncController::schedulePush);
}

void AddonsSyncController::schedulePush()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) return;
    if (!m_debounce.isActive()) m_debounce.start();
}

void AddonsSyncController::pullNow()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) {
        emit pullFinished(false, 0);
        return;
    }
    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con](bool ok, int status, const QJsonDocument& doc,
                               QByteArray) {
        disconnect(*con);
        --m_inFlight;
        int applied = 0;
        if (ok && status == 200 && doc.isArray()) {
            const QJsonArray rows = doc.array();
            applied = static_cast<int>(rows.size());
            m_registry->applyServerRows(rows);
        }
        emit pullFinished(ok, applied);
    });
    m_client->get(QStringLiteral(
        "addons?profile_id=eq.%1&select=url,name,enabled,sort_order"
        "&order=sort_order.asc").arg(m_profileId));
}

void AddonsSyncController::doPush()
{
    if (!m_cfg.valid() || !signedIn() || m_inFlight != 0) return;

    const QJsonArray rows = m_registry->exportServerRows();
    const QByteArray sig = QCryptographicHash::hash(
        QJsonDocument(rows).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256);
    if (m_lastPushValid && sig == m_lastPushSig) return;   // dedupe

    QJsonObject params{
        {QStringLiteral("p_profile_id"), m_profileId},
        {QStringLiteral("p_addons"), rows},
        {QStringLiteral("p_origin_client_id"), originId()},
    };

    ++m_inFlight;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &SyncRpcClient::finished, this,
                   [this, con, sig](bool ok, int, const QJsonDocument&,
                                    QByteArray) {
        disconnect(*con);
        --m_inFlight;
        if (ok) { m_lastPushValid = true; m_lastPushSig = sig; }
        emit pushFinished(ok);
    });
    m_client->call(QStringLiteral("sync_push_addons"), params);
}

} // namespace nuvio::authsync