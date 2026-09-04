#pragma once

// Membership models (fork features/membership parity): supporter tiers,
// cosmetic entitlements, cached access payload, overview shape +
// response mapping. Pure codec/parse layer; the repositories live in
// MemberAccess.h / MembershipOverview.h.

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace nuvio::membership {

enum class MemberTier {
    None,
    Supporter,
    SupporterPlus,
};

[[nodiscard]] QString memberTierName(MemberTier tier);
[[nodiscard]] MemberTier memberTierFromName(const QString& name);
/// Display names ("Supporter"/"Supporter Plus", fork tier strings).
[[nodiscard]] QString memberTierDisplayName(MemberTier tier);

enum class CosmeticEntitlement {
    GoldTheme,
    JadeTheme,
    RoseGoldTheme,
    ArcticBlueTheme,
    GraphiteTheme,
    ProfileBackgrounds,
    ProfileAvatars,
};

[[nodiscard]] QString entitlementName(CosmeticEntitlement entitlement);
[[nodiscard]] bool entitlementFromName(const QString& name,
                                       CosmeticEntitlement& out);

/// Cached access payload (StoredMemberAccess parity):
/// {"userId":..,"tier":"SUPPORTER"|null,"entitlements":[...]}.
/// Garbage decodes to null (runCatching parity).
struct StoredAccess {
    QString userId;
    MemberTier tier = MemberTier::None;
    QList<CosmeticEntitlement> entitlements;
    bool valid = false;   // false = absent/garbage/unknown-tier
};

[[nodiscard]] QString encodeStoredAccess(const QString& userId,
                                         MemberTier tier,
                                         const QList<CosmeticEntitlement>&
                                             entitlements);
[[nodiscard]] StoredAccess decodeStoredAccess(const QString& raw);

/// Overview response mapping (MembershipOverviewResponse parity with
/// the takeIf(hasActiveGrant)/takeIf(hasLifetimeGrant) guards folded
/// into the parse, exactly like the fork's data-source mapping).
struct MembershipOverviewData {
    QString status = QStringLiteral("inactive");
    MemberTier tier = MemberTier::None;
    QString verifiedAt;
    QString supporterSince;
    bool providerConnected = false;
    bool hasSubscription = false;
    bool subscriptionActive = false;
    QString subscriptionStatus;
    QString billingProvider;
    MemberTier membershipLevel = MemberTier::None;
    QString currentPeriodEnd;
    bool cancelsAtPeriodEnd = false;
    bool hasActiveGrant = false;
    bool grantIsLifetime = false;
    QString grantExpiresAt;
    QString grantKind;
    MemberTier grantTier = MemberTier::None;
    QString grantSource;
    bool hasLifetimeGrant = false;
    MemberTier lifetimeGrantTier = MemberTier::None;

    [[nodiscard]] bool active() const
    {
        return status == QLatin1String("active") &&
               tier != MemberTier::None;
    }
};

/// Parses the first row of a get_my_membership_overview response array
/// (empty array -> default inactive overview, fork parity).
[[nodiscard]] MembershipOverviewData
parseMembershipOverview(const QByteArray& body);
/// Parses the first row of a get_my_member_access response array
/// (empty/unknown-tier -> None access, fork parity).
[[nodiscard]] bool parseMemberAccess(const QByteArray& body,
                                     MemberTier& tierOut,
                                     QList<CosmeticEntitlement>&
                                         entitlementsOut);

/// "MMM D, YYYY" donation dates (fork formatDonationDate parity, via
/// the system locale's short month names).
[[nodiscard]] QString formatDonationDate(const QString& raw);

} // namespace nuvio::membership
