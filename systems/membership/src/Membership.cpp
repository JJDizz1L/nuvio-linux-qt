#include "nuvio/membership/Membership.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>

namespace nuvio::membership {

QString memberTierName(MemberTier tier)
{
    switch (tier) {
    case MemberTier::Supporter: return QStringLiteral("SUPPORTER");
    case MemberTier::SupporterPlus: return QStringLiteral("SUPPORTER_PLUS");
    case MemberTier::None: break;
    }
    return {};
}

MemberTier memberTierFromName(const QString& name)
{
    if (name == QLatin1String("SUPPORTER")) return MemberTier::Supporter;
    if (name == QLatin1String("SUPPORTER_PLUS"))
        return MemberTier::SupporterPlus;
    return MemberTier::None;
}

QString memberTierDisplayName(MemberTier tier)
{
    switch (tier) {
    case MemberTier::Supporter: return QStringLiteral("Supporter");
    case MemberTier::SupporterPlus:
        return QStringLiteral("Supporter Plus");
    case MemberTier::None: break;
    }
    return {};
}

QString entitlementName(CosmeticEntitlement entitlement)
{
    switch (entitlement) {
    case CosmeticEntitlement::GoldTheme:
        return QStringLiteral("GOLD_THEME");
    case CosmeticEntitlement::JadeTheme:
        return QStringLiteral("JADE_THEME");
    case CosmeticEntitlement::RoseGoldTheme:
        return QStringLiteral("ROSE_GOLD_THEME");
    case CosmeticEntitlement::ArcticBlueTheme:
        return QStringLiteral("ARCTIC_BLUE_THEME");
    case CosmeticEntitlement::GraphiteTheme:
        return QStringLiteral("GRAPHITE_THEME");
    case CosmeticEntitlement::ProfileBackgrounds:
        return QStringLiteral("PROFILE_BACKGROUNDS");
    case CosmeticEntitlement::ProfileAvatars:
        return QStringLiteral("PROFILE_AVATARS");
    }
    return {};
}

bool entitlementFromName(const QString& name, CosmeticEntitlement& out)
{
    if (name == QLatin1String("GOLD_THEME")) {
        out = CosmeticEntitlement::GoldTheme;
        return true;
    }
    if (name == QLatin1String("JADE_THEME")) {
        out = CosmeticEntitlement::JadeTheme;
        return true;
    }
    if (name == QLatin1String("ROSE_GOLD_THEME")) {
        out = CosmeticEntitlement::RoseGoldTheme;
        return true;
    }
    if (name == QLatin1String("ARCTIC_BLUE_THEME")) {
        out = CosmeticEntitlement::ArcticBlueTheme;
        return true;
    }
    if (name == QLatin1String("GRAPHITE_THEME")) {
        out = CosmeticEntitlement::GraphiteTheme;
        return true;
    }
    if (name == QLatin1String("PROFILE_BACKGROUNDS")) {
        out = CosmeticEntitlement::ProfileBackgrounds;
        return true;
    }
    if (name == QLatin1String("PROFILE_AVATARS")) {
        out = CosmeticEntitlement::ProfileAvatars;
        return true;
    }
    return false;
}

QString encodeStoredAccess(const QString& userId, MemberTier tier,
                           const QList<CosmeticEntitlement>& entitlements)
{
    QJsonArray names;
    for (CosmeticEntitlement e : entitlements)
        names.append(entitlementName(e));
    QJsonObject root;
    root.insert(QStringLiteral("userId"), userId);
    if (tier != MemberTier::None)
        root.insert(QStringLiteral("tier"), memberTierName(tier));
    root.insert(QStringLiteral("entitlements"), names);
    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
}

StoredAccess decodeStoredAccess(const QString& raw)
{
    StoredAccess out;
    if (raw.trimmed().isEmpty()) return out;
    const QJsonObject root =
        QJsonDocument::fromJson(raw.toUtf8()).object();
    if (root.isEmpty()) return out;
    out.userId = root.value(QStringLiteral("userId")).toString();
    if (out.userId.isEmpty()) return out;
    const QString tierName =
        root.value(QStringLiteral("tier")).toString();
    // Unknown-tier rows decode to None access (fork toMemberAccess
    // parity: null tier -> MemberAccess.None).
    if (!tierName.isEmpty() &&
        memberTierFromName(tierName) == MemberTier::None)
        return out;
    out.tier = memberTierFromName(tierName);
    for (const QJsonValue& v :
         root.value(QStringLiteral("entitlements")).toArray()) {
        CosmeticEntitlement e;
        if (entitlementFromName(v.toString(), e)) out.entitlements.append(e);
    }
    out.valid = true;
    return out;
}

namespace {

[[nodiscard]] bool jsonTrue(const QJsonObject& o, const char* key)
{
    return o.value(QLatin1String(key)).toBool(false);
}

[[nodiscard]] QString jsonText(const QJsonObject& o, const char* key)
{
    return o.value(QLatin1String(key)).toString();
}

} // namespace

MembershipOverviewData parseMembershipOverview(const QByteArray& body)
{
    MembershipOverviewData out;
    const QJsonArray rows = QJsonDocument::fromJson(body).array();
    if (rows.isEmpty()) return out;   // empty -> inactive default
    const QJsonObject r = rows.first().toObject();
    const bool hasActiveGrant = jsonTrue(r, "has_active_grant");
    const bool subscriptionActive =
        jsonTrue(r, "subscription_access_active");
    const bool hasLifetimeGrant =
        hasActiveGrant && jsonTrue(r, "has_lifetime_grant");
    out.status = jsonText(r, "status");
    if (out.status.isEmpty()) out.status = QStringLiteral("inactive");
    out.tier = memberTierFromName(jsonText(r, "tier"));
    out.verifiedAt = jsonText(r, "verified_at");
    out.supporterSince = jsonText(r, "supporter_since");
    out.providerConnected = jsonTrue(r, "provider_connected");
    out.hasSubscription = jsonTrue(r, "has_subscription");
    out.subscriptionActive = subscriptionActive;
    out.subscriptionStatus = jsonText(r, "subscription_status");
    out.billingProvider = jsonText(r, "provider");
    out.membershipLevel = memberTierFromName(jsonText(r, "membership_level"));
    out.currentPeriodEnd = jsonText(r, "current_period_end");
    out.cancelsAtPeriodEnd =
        subscriptionActive && jsonTrue(r, "cancels_at_period_end");
    out.hasActiveGrant = hasActiveGrant;
    out.grantIsLifetime =
        hasActiveGrant && jsonTrue(r, "grant_is_lifetime");
    if (hasActiveGrant) {
        out.grantExpiresAt = jsonText(r, "grant_expires_at");
        out.grantKind = jsonText(r, "grant_kind");
        out.grantTier = memberTierFromName(jsonText(r, "grant_tier"));
        out.grantSource = jsonText(r, "grant_source");
    }
    out.hasLifetimeGrant = hasLifetimeGrant;
    if (hasLifetimeGrant)
        out.lifetimeGrantTier =
            memberTierFromName(jsonText(r, "lifetime_grant_tier"));
    return out;
}

bool parseMemberAccess(const QByteArray& body, MemberTier& tierOut,
                       QList<CosmeticEntitlement>& entitlementsOut)
{
    tierOut = MemberTier::None;
    entitlementsOut.clear();
    const QJsonArray rows = QJsonDocument::fromJson(body).array();
    if (rows.isEmpty()) return true;   // empty -> None access, not error
    const QJsonObject r = rows.first().toObject();
    const MemberTier tier =
        memberTierFromName(jsonText(r, "tier"));
    if (tier == MemberTier::None) return true;   // unknown tier -> None
    tierOut = tier;
    for (const QJsonValue& v :
         r.value(QStringLiteral("entitlements")).toArray()) {
        CosmeticEntitlement e;
        if (entitlementFromName(v.toString(), e))
            entitlementsOut.append(e);
    }
    return true;
}

QString formatDonationDate(const QString& raw)
{
    // "YYYY-MM-DD[..]" -> "Mon D, YYYY"; anything else passes through.
    const QString datePart = raw.section(u'T', 0, 0);
    const QStringList parts = datePart.split(u'-');
    if (parts.size() != 3) return raw;
    bool okYear = false, okMonth = false, okDay = false;
    const int year = parts[0].toInt(&okYear);
    const int month = parts[1].toInt(&okMonth);
    const int day = parts[2].toInt(&okDay);
    if (!okYear || !okMonth || !okDay) return raw;
    if (!QDate::isValid(year, month, day)) return raw;
    const QString monthName =
        QLocale().standaloneMonthName(month, QLocale::ShortFormat);
    return QStringLiteral("%1 %2, %3")
        .arg(monthName, QString::number(day), QString::number(year));
}

} // namespace nuvio::membership
