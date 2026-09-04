#pragma once

// Supporter access repository (fork MemberAccessRepository parity):
// tier + cosmetic entitlements over the get_my_member_access RPC with
// a userId-keyed cached payload, 1/2/4s fetch retry, and 15-minute
// re-verification. Signed-out sessions always read None access (this
// line has no anonymous tier). Entitlement-gated surfaces (supporter
// themes, profile backgrounds/avatars) consult hasEntitlement() when
// they land; nothing gates on it yet.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "nuvio/membership/Membership.h"

namespace nuvio::authsync {
class AuthService;
class SyncRpcClient;
} // namespace nuvio::authsync

namespace nuvio::membership {

class MemberAccess final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString tierName READ tierName NOTIFY changed)
    Q_PROPERTY(QStringList entitlements READ entitlements NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)

public:
    explicit MemberAccess(nuvio::authsync::AuthService* auth,
                          QObject* parent = nullptr);

    [[nodiscard]] QString tierName() const
    {
        return memberTierName(m_tier);
    }
    [[nodiscard]] QStringList entitlements() const;
    [[nodiscard]] bool active() const
    {
        return m_tier != MemberTier::None;
    }
    Q_INVOKABLE bool hasEntitlement(const QString& name) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshIfStale();
    /// Sign-out reset (clears the cached payload too).
    Q_INVOKABLE void clearLocalState();

signals:
    void changed();

private:
    void ensureStarted();
    void onAuthChanged();
    void loadAccess(quint64 generation);
    void fetchWithRetry(int attempt, quint64 generation);
    void applyRemote(const QString& userId, MemberTier tier,
                     const QList<CosmeticEntitlement>& entitlements,
                     quint64 generation);
    void setAccess(MemberTier tier,
                   const QList<CosmeticEntitlement>& entitlements);
    void loadStored();
    void saveStored(const QString& userId);

    nuvio::authsync::AuthService* m_auth = nullptr;
    nuvio::authsync::SyncRpcClient* m_client = nullptr;
    QTimer m_verifyTimer;

    MemberTier m_tier = MemberTier::None;
    QList<CosmeticEntitlement> m_entitlements;
    QString m_verifiedUserId;
    qint64 m_verifiedAtMs = 0;
    quint64 m_generation = 0;
    bool m_started = false;
};

} // namespace nuvio::membership
