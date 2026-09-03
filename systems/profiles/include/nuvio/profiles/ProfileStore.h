#pragma once

// Profiles data layer (P7): verbatim Compose shapes (StoredProfilePayload,
// NuvioProfile snake_case, ProfilePushPayload, CachedProfilePinPayload,
// ProfileLockState) + PIN crypto (sha256Hex("profile:<i>:<salt>:<pin>"),
// 2xULong-hex salts). PINs travel plaintext to the server RPCs (Compose
// parity); only the offline cache is salted+hashed. MAX_PROFILES = 6
// (server-validated 1..6).

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace nuvio::profiles {

constexpr int kMaxProfiles = 6;

struct NuvioProfile {
    QString id;
    QString userId;
    int profileIndex = 1;
    QString name;
    QString avatarColorHex = "#1E88E5";   // Compose default
    QString avatarId;
    QString avatarUrl;
    QString profileBackgroundId;
    QString profileBackgroundUrl;
    bool usesPrimaryAddons = false;
    bool usesPrimaryPlugins = false;
    bool pinEnabled = false;
    QString pinLockedUntil;
    QString createdAt;
    QString updatedAt;
};

struct StoredProfilesPayload {
    QString userId;
    int activeProfileIndex = 1;
    bool hasEverSelectedProfile = false;
    bool rememberLastProfileEnabled = false;
    QList<NuvioProfile> profiles;
};

/// sha256Hex("profile:<index>:<salt>:<pin>") (ProfilePinCache parity).
[[nodiscard]] QString hashProfilePin(int profileIndex, const QString& salt,
                                     const QString& pin);
/// Two ULong hex strings concatenated (generateProfilePinSalt parity).
[[nodiscard]] QString generateProfilePinSalt();

class ProfileCodec final {
public:
    [[nodiscard]] static StoredProfilesPayload decodeStored(
        const QString& json);
    [[nodiscard]] static QString encodeStored(
        const StoredProfilesPayload& payload);
    [[nodiscard]] static NuvioProfile profileFromJson(const QJsonObject& o);
    [[nodiscard]] static QJsonObject profileToPushJson(
        int profileIndex, const QString& name, const QString& avatarColorHex,
        bool usesPrimaryAddons, bool usesPrimaryPlugins,
        const QString& avatarId, const QString& avatarUrl,
        const QString& backgroundId, const QString& backgroundUrl);
    [[nodiscard]] static QJsonObject profileToPushJson(
        const NuvioProfile& profile);
};

/// Per-profile salted PIN cache (profile_pin_cache store, key
/// profile_pin_<index>, payload {salt,digest,profileUpdatedAt}).
class ProfilePinCache final {
public:
    [[nodiscard]] static QString loadPayload(int profileIndex);
    static void savePayload(int profileIndex, const QString& payload);
    static void removePayload(int profileIndex);
};

} // namespace nuvio::profiles
