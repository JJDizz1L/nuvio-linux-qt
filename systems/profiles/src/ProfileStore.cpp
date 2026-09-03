#include "nuvio/profiles/ProfileStore.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::profiles {

QString hashProfilePin(int profileIndex, const QString& salt,
                       const QString& pin)
{
    const QByteArray raw = QStringLiteral("profile:%1:%2:%3")
                               .arg(profileIndex)
                               .arg(salt, pin)
                               .toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

QString generateProfilePinSalt()
{
    const auto h = [](quint64 v) {
        return QString::number(v, 16);
    };
    return h(QRandomGenerator::system()->generate64()) +
           h(QRandomGenerator::system()->generate64());
}

StoredProfilesPayload ProfileCodec::decodeStored(const QString& json)
{
    StoredProfilesPayload out;   // runCatching parity: garbage -> defaults
    const QJsonObject root =
        QJsonDocument::fromJson(json.toUtf8()).object();
    if (root.isEmpty()) return out;
    out.userId = root.value(QStringLiteral("userId")).toString();
    out.activeProfileIndex =
        root.value(QStringLiteral("activeProfileIndex")).toInt(1);
    if (out.activeProfileIndex < 1 || out.activeProfileIndex > kMaxProfiles)
        out.activeProfileIndex = 1;
    out.hasEverSelectedProfile =
        root.value(QStringLiteral("hasEverSelectedProfile")).toBool(false);
    out.rememberLastProfileEnabled =
        root.value(QStringLiteral("rememberLastProfileEnabled")).toBool(false);
    for (const QJsonValue& v :
         root.value(QStringLiteral("profiles")).toArray())
        out.profiles.append(profileFromJson(v.toObject()));
    return out;
}

QString ProfileCodec::encodeStored(const StoredProfilesPayload& payload)
{
    QJsonArray arr;
    for (const NuvioProfile& p : payload.profiles) {
        arr.append(QJsonObject{
            {QStringLiteral("id"), p.id},
            {QStringLiteral("user_id"), p.userId},
            {QStringLiteral("profile_index"), p.profileIndex},
            {QStringLiteral("name"), p.name},
            {QStringLiteral("avatar_color_hex"), p.avatarColorHex},
            {QStringLiteral("avatar_id"), p.avatarId},
            {QStringLiteral("avatar_url"), p.avatarUrl},
            {QStringLiteral("profile_background_id"), p.profileBackgroundId},
            {QStringLiteral("profile_background_url"), p.profileBackgroundUrl},
            {QStringLiteral("uses_primary_addons"), p.usesPrimaryAddons},
            {QStringLiteral("uses_primary_plugins"), p.usesPrimaryPlugins},
            {QStringLiteral("pin_enabled"), p.pinEnabled},
            {QStringLiteral("pin_locked_until"), p.pinLockedUntil},
            {QStringLiteral("created_at"), p.createdAt},
            {QStringLiteral("updated_at"), p.updatedAt},
        });
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{
                          {QStringLiteral("userId"), payload.userId},
                          {QStringLiteral("activeProfileIndex"),
                           payload.activeProfileIndex},
                          {QStringLiteral("hasEverSelectedProfile"),
                           payload.hasEverSelectedProfile},
                          {QStringLiteral("rememberLastProfileEnabled"),
                           payload.rememberLastProfileEnabled},
                          {QStringLiteral("profiles"), arr},
                      })
            .toJson(QJsonDocument::Compact));
}

NuvioProfile ProfileCodec::profileFromJson(const QJsonObject& o)
{
    NuvioProfile p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.userId = o.value(QStringLiteral("user_id")).toString();
    p.profileIndex = o.value(QStringLiteral("profile_index")).toInt(1);
    p.name = o.value(QStringLiteral("name")).toString();
    p.avatarColorHex =
        o.value(QStringLiteral("avatar_color_hex")).toString("#1E88E5");
    p.avatarId = o.value(QStringLiteral("avatar_id")).toString();
    p.avatarUrl = o.value(QStringLiteral("avatar_url")).toString();
    p.profileBackgroundId =
        o.value(QStringLiteral("profile_background_id")).toString();
    p.profileBackgroundUrl =
        o.value(QStringLiteral("profile_background_url")).toString();
    p.usesPrimaryAddons =
        o.value(QStringLiteral("uses_primary_addons")).toBool(false);
    p.usesPrimaryPlugins =
        o.value(QStringLiteral("uses_primary_plugins")).toBool(false);
    p.pinEnabled = o.value(QStringLiteral("pin_enabled")).toBool(false);
    p.pinLockedUntil =
        o.value(QStringLiteral("pin_locked_until")).toString();
    p.createdAt = o.value(QStringLiteral("created_at")).toString();
    p.updatedAt = o.value(QStringLiteral("updated_at")).toString();
    return p;
}

QJsonObject ProfileCodec::profileToPushJson(
    int profileIndex, const QString& name, const QString& avatarColorHex,
    bool usesPrimaryAddons, bool usesPrimaryPlugins, const QString& avatarId,
    const QString& avatarUrl, const QString& backgroundId,
    const QString& backgroundUrl)
{
    QJsonObject o{
        {QStringLiteral("profile_index"), profileIndex},
        {QStringLiteral("name"), name},
        {QStringLiteral("avatar_color_hex"), avatarColorHex},
        {QStringLiteral("uses_primary_addons"), usesPrimaryAddons},
        {QStringLiteral("uses_primary_plugins"), usesPrimaryPlugins},
    };
    // Nullables ride as null when empty (kotlinx explicit-null parity).
    o.insert(QStringLiteral("avatar_id"),
             avatarId.isEmpty() ? QJsonValue() : QJsonValue(avatarId));
    o.insert(QStringLiteral("avatar_url"),
             avatarUrl.isEmpty() ? QJsonValue() : QJsonValue(avatarUrl));
    o.insert(QStringLiteral("profile_background_id"),
             backgroundId.isEmpty() ? QJsonValue()
                                    : QJsonValue(backgroundId));
    o.insert(QStringLiteral("profile_background_url"),
             backgroundUrl.isEmpty() ? QJsonValue()
                                     : QJsonValue(backgroundUrl));
    return o;
}

QJsonObject ProfileCodec::profileToPushJson(const NuvioProfile& profile)
{
    return profileToPushJson(
        profile.profileIndex, profile.name, profile.avatarColorHex,
        profile.usesPrimaryAddons, profile.usesPrimaryPlugins,
        profile.avatarId, profile.avatarUrl, profile.profileBackgroundId,
        profile.profileBackgroundUrl);
}

QString ProfilePinCache::loadPayload(int profileIndex)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(
            "profile_pin_cache"));
    return QString::fromStdString(
        store.getString("profile_pin_" + std::to_string(profileIndex))
            .value_or(""));
}

void ProfilePinCache::savePayload(int profileIndex, const QString& payload)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(
            "profile_pin_cache"));
    store.putString("profile_pin_" + std::to_string(profileIndex),
                    payload.toStdString());
}

void ProfilePinCache::removePayload(int profileIndex)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(
            "profile_pin_cache"));
    store.remove("profile_pin_" + std::to_string(profileIndex));
}

} // namespace nuvio::profiles
