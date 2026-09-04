#include "nuvio/membership/MembershipOverview.h"

#include <QJsonDocument>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/AuthService.h"
#include "nuvio/authsync/SyncRpcClient.h"

namespace nuvio::membership {

MembershipOverview::MembershipOverview(nuvio::authsync::AuthService* auth,
                                       QObject* parent)
    : QObject(parent), m_auth(auth)
{
    Q_ASSERT(m_auth);
    m_client = new nuvio::authsync::SyncRpcClient(
        nuvio::authsync::AuthConfig::load(),
        [this] { return m_auth->accessToken(); }, this);
}

void MembershipOverview::refresh()
{
    ensureStarted();
    ++m_generation;
    loadOverview(m_generation);
}

void MembershipOverview::ensureStarted()
{
    if (m_started) return;
    m_started = true;
    connect(m_auth, &nuvio::authsync::AuthService::stateChanged, this,
            &MembershipOverview::onAuthChanged);
    onAuthChanged();
}

void MembershipOverview::onAuthChanged()
{
    ++m_generation;
    loadOverview(m_generation);
}

void MembershipOverview::loadOverview(quint64 generation)
{
    const bool signedIn =
        m_auth->sessionActive() && !m_auth->userId().isEmpty();
    if (!signedIn) {
        if (generation != m_generation) return;
        m_currentUserId.clear();
        m_overview = toVariant(MembershipOverviewData{});
        m_loading = false;
        m_refreshing = false;
        m_errorMessage.clear();
        emit changed();
        return;
    }
    // Same-user refreshes keep the previous overview (isRefreshing,
    // fork parity); user switches reset to loading.
    const bool sameUser = m_currentUserId == m_auth->userId();
    m_currentUserId = m_auth->userId();
    if (generation == m_generation) {
        m_loading = !sameUser || m_overview.isEmpty();
        m_refreshing = sameUser && !m_overview.isEmpty();
        m_errorMessage.clear();
        emit changed();
    }
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con, generation](bool ok, int,
                                           const QJsonDocument& doc,
                                           QByteArray raw) {
                       disconnect(*con);
                       if (generation != m_generation) return;
                       if (ok) {
                           m_overview = toVariant(
                               parseMembershipOverview(doc.toJson(
                                   QJsonDocument::Compact)));
                           m_loading = false;
                           m_refreshing = false;
                           m_errorMessage.clear();
                       } else {
                           // Keep the previous overview on failure.
                           m_loading = false;
                           m_refreshing = false;
                           m_errorMessage =
                               QString::fromUtf8(raw).trimmed();
                           if (m_errorMessage.isEmpty())
                               m_errorMessage = tr("Unable to load "
                                                   "membership status. "
                                                   "Please try again.");
                       }
                       emit changed();
                   });
    m_client->call(QStringLiteral("get_my_membership_overview"),
                   QJsonObject{});
}

QVariantMap MembershipOverview::toVariant(
    const MembershipOverviewData& data)
{
    return QVariantMap{
        {QStringLiteral("status"), data.status},
        {QStringLiteral("tier"), memberTierName(data.tier)},
        {QStringLiteral("tierDisplay"),
         memberTierDisplayName(data.tier)},
        {QStringLiteral("verifiedAt"), data.verifiedAt},
        {QStringLiteral("supporterSince"),
         data.supporterSince.isEmpty()
             ? QString()
             : formatDonationDate(data.supporterSince)},
        {QStringLiteral("providerConnected"), data.providerConnected},
        {QStringLiteral("hasSubscription"), data.hasSubscription},
        {QStringLiteral("subscriptionActive"), data.subscriptionActive},
        {QStringLiteral("subscriptionStatus"), data.subscriptionStatus},
        {QStringLiteral("billingProvider"), data.billingProvider},
        {QStringLiteral("membershipLevel"),
         memberTierName(data.membershipLevel)},
        {QStringLiteral("membershipLevelDisplay"),
         memberTierDisplayName(data.membershipLevel)},
        {QStringLiteral("currentPeriodEnd"), data.currentPeriodEnd},
        {QStringLiteral("cancelsAtPeriodEnd"), data.cancelsAtPeriodEnd},
        {QStringLiteral("hasActiveGrant"), data.hasActiveGrant},
        {QStringLiteral("grantIsLifetime"), data.grantIsLifetime},
        {QStringLiteral("grantExpiresAt"), data.grantExpiresAt},
        {QStringLiteral("grantKind"), data.grantKind},
        {QStringLiteral("grantTier"), memberTierName(data.grantTier)},
        {QStringLiteral("grantTierDisplay"),
         memberTierDisplayName(data.grantTier)},
        {QStringLiteral("grantSource"), data.grantSource},
        {QStringLiteral("hasLifetimeGrant"), data.hasLifetimeGrant},
        {QStringLiteral("lifetimeGrantTier"),
         memberTierName(data.lifetimeGrantTier)},
        {QStringLiteral("active"), data.active()},
    };
}

} // namespace nuvio::membership
