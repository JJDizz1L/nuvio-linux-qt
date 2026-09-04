#pragma once

// Membership overview repository (fork MembershipOverviewRepository
// parity): status-card state over get_my_membership_overview. Keeps the
// previous overview across user-preserving refreshes (isLoading vs
// isRefreshing split) and surfaces fetch errors without clearing.

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "nuvio/membership/Membership.h"

namespace nuvio::authsync {
class AuthService;
class SyncRpcClient;
} // namespace nuvio::authsync

namespace nuvio::membership {

class MembershipOverview final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap overview READ overview NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)

public:
    explicit MembershipOverview(nuvio::authsync::AuthService* auth,
                                QObject* parent = nullptr);

    [[nodiscard]] QVariantMap overview() const { return m_overview; }
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] bool refreshing() const { return m_refreshing; }
    [[nodiscard]] QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    void ensureStarted();
    void onAuthChanged();
    void loadOverview(quint64 generation);
    [[nodiscard]] static QVariantMap toVariant(
        const MembershipOverviewData& data);

    nuvio::authsync::AuthService* m_auth = nullptr;
    nuvio::authsync::SyncRpcClient* m_client = nullptr;

    QVariantMap m_overview;
    bool m_loading = true;
    bool m_refreshing = false;
    QString m_errorMessage;
    QString m_currentUserId;
    quint64 m_generation = 0;
    bool m_started = false;
};

} // namespace nuvio::membership
