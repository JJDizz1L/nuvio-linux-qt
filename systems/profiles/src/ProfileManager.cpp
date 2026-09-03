#include "nuvio/profiles/ProfileManager.h"

#include <algorithm>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncIdentity.h"

namespace nuvio::profiles {

namespace {
bool lockActive(const QString& lockedUntilIso)
{
    if (lockedUntilIso.trimmed().isEmpty()) return false;
    // ISO-8601 UTC compares lexicographically; unparseable = enforce.
    const QDateTime until = QDateTime::fromString(
        lockedUntilIso.trimmed(), Qt::ISODateWithMs);
    if (!until.isValid()) {
        const QDateTime fallback = QDateTime::fromString(
            lockedUntilIso.trimmed(), Qt::ISODate);
        if (!fallback.isValid()) return true;
        return fallback > QDateTime::currentDateTimeUtc();
    }
    return until > QDateTime::currentDateTimeUtc();
}
} // namespace

ProfileManager::ProfileManager(nuvio::authsync::AuthConfig cfg, TokenProvider token,
                               QObject* parent)
    : QObject(parent),
      m_cfg(std::move(cfg)),
      m_token(std::move(token)),
      m_client(new nuvio::authsync::SyncRpcClient(m_cfg, [this] { return m_token(); }, this))
{}

QVariantList ProfileManager::profilesVariant() const
{
    QVariantList out;
    for (const NuvioProfile& p : m_profiles)
        out.append(QVariantMap{
            {QStringLiteral("index"), p.profileIndex},
            {QStringLiteral("name"), p.name},
            {QStringLiteral("avatarColorHex"), p.avatarColorHex},
            {QStringLiteral("avatarId"), p.avatarId},
            {QStringLiteral("avatarUrl"), p.avatarUrl},
            {QStringLiteral("pinEnabled"), p.pinEnabled},
            {QStringLiteral("locked"),
             lockActive(p.pinLockedUntil) ||
                 lockActive(m_lockedUntil.value(p.profileIndex))},
        });
    return out;
}

std::optional<NuvioProfile> ProfileManager::profile(int index) const
{
    for (const NuvioProfile& p : m_profiles) {
        if (p.profileIndex == index) return p;
    }
    return std::nullopt;
}

void ProfileManager::loadLocal()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("profiles"));
    const auto raw = store.getString("profiles");
    StoredProfilesPayload payload;
    if (raw && !raw->empty())
        payload = ProfileCodec::decodeStored(QString::fromStdString(*raw));
    if (!m_userId.isEmpty() && !payload.userId.isEmpty() &&
        payload.userId != m_userId) {
        payload = StoredProfilesPayload{};   // foreign account residue
    }
    m_profiles = payload.profiles;
    m_activeIndex = payload.activeProfileIndex;
    m_hasEverSelected = payload.hasEverSelectedProfile;
    m_rememberLastProfile = payload.rememberLastProfileEnabled;
    m_loaded = true;
    nuvio::settings::ActiveProfile::setId(m_activeIndex);
    emit changed();
}

void ProfileManager::setAuthUserId(const QString& userId)
{
    if (m_userId == userId) return;
    m_userId = userId;
    if (userId.isEmpty()) {
        // Signed out: keep the local payload on disk but reset the live
        // state to a fresh profile-1 (Compose clears in-memory state).
        m_profiles.clear();
        m_activeIndex = 1;
        m_hasEverSelected = false;
        m_loaded = false;
        m_verifiedInSession.clear();
        nuvio::settings::ActiveProfile::setId(1);
        emit changed();
        return;
    }
    loadLocal();
}

void ProfileManager::persist()
{
    if (m_userId.isEmpty()) return;   // Compose persists authenticated only
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("profiles"));
    StoredProfilesPayload payload;
    payload.userId = m_userId;
    payload.activeProfileIndex = m_activeIndex;
    payload.hasEverSelectedProfile = m_hasEverSelected;
    payload.rememberLastProfileEnabled = m_rememberLastProfile;
    payload.profiles = m_profiles;
    store.putString("profiles",
                    ProfileCodec::encodeStored(payload).toStdString());
}

void ProfileManager::applyActive(int profileIndex, bool markSelected)
{
    m_activeIndex = profileIndex;
    if (markSelected) m_hasEverSelected = true;
    nuvio::settings::ActiveProfile::setId(profileIndex);
    persist();
    emit changed();
    emit activeProfileChanged(profileIndex);
}

void ProfileManager::switchToProfile(int profileIndex)
{
    const auto target = profile(profileIndex);
    if (!target) {
        emit operationResult(false, QStringLiteral("Unknown profile"));
        return;
    }
    if (target->pinEnabled && !m_verifiedInSession.contains(profileIndex)) {
        emit operationResult(false, QStringLiteral("PIN required"));
        return;
    }
    applyActive(profileIndex, true);
    emit operationResult(true, {});
}

QString ProfileManager::originId()
{
    nuvio::settings::PropertiesStore idStore(
        nuvio::settings::PropertiesStore::defaultPath(
            "sync_client_identity"));
    return nuvio::settings::SyncIdentity::currentClientId(idStore);
}

bool ProfileManager::pinNeedsOnline(int profileIndex) const
{
    for (const NuvioProfile& p : m_profiles) {
        if (p.profileIndex != profileIndex) continue;
        if (!p.pinEnabled) return false;
        return ProfilePinCache::loadPayload(profileIndex).trimmed().isEmpty();
    }
    return false;
}

void ProfileManager::verifyPin(int profileIndex, const QString& pin)
{
    const auto target = profile(profileIndex);
    if (!target) {
        emit pinResult(false, QStringLiteral("Unknown profile"));
        return;
    }
    if (!target->pinEnabled) {
        m_verifiedInSession.insert(profileIndex);
        emit pinResult(true, {});
        return;
    }
    if (lockActive(target->pinLockedUntil) ||
        lockActive(m_lockedUntil.value(profileIndex))) {
        emit pinResult(false, QStringLiteral("Profile is temporarily locked"));
        return;
    }
    if (signedIn() && m_cfg.valid()) {
        auto con = std::make_shared<QMetaObject::Connection>();
        *con = connect(
            m_client, &nuvio::authsync::SyncRpcClient::finished, this,
            [this, con, profileIndex, pin](bool ok, int,
                                           const QJsonDocument& doc,
                                           QByteArray) {
                disconnect(*con);
                const bool unlocked =
                    ok && doc.object()
                              .value(QStringLiteral("unlocked"))
                              .toBool(false);
                if (unlocked) {
                    rememberVerifiedPin(profileIndex, pin);
                    m_verifiedInSession.insert(profileIndex);
                    emit pinResult(true, {});
                    return;
                }
                const QString message =
                    doc.object()
                        .value(QStringLiteral("message"))
                        .toString(QStringLiteral("Incorrect PIN"));
                emit pinResult(false, ok ? message
                                         : QStringLiteral("Network error"));
            });
        m_client->call(QString::fromLatin1("verify_profile_pin"),
                       QJsonObject{
                           {QStringLiteral("p_profile_id"), profileIndex},
                           {QStringLiteral("p_pin"), pin},
                       });
        return;
    }
    // Offline: salted cache (Compose verifyPinLocally parity).
    if (pinNeedsOnline(profileIndex)) {
        emit pinResult(false, QStringLiteral("PIN needs online check"));
        return;
    }
    const QJsonObject cached = QJsonDocument::fromJson(
                                   ProfilePinCache::loadPayload(profileIndex)
                                       .toUtf8())
                                   .object();
    const QString updatedAt = target->updatedAt;
    if (!cached.value(QStringLiteral("profileUpdatedAt")).toString().isEmpty() &&
        !updatedAt.isEmpty() &&
        cached.value(QStringLiteral("profileUpdatedAt")).toString() !=
            updatedAt) {
        ProfilePinCache::removePayload(profileIndex);
        emit pinResult(false, QStringLiteral("PIN changed - sign in again"));
        return;
    }
    const QString digest = hashProfilePin(
        profileIndex, cached.value(QStringLiteral("salt")).toString(), pin);
    if (digest == cached.value(QStringLiteral("digest")).toString()) {
        m_verifiedInSession.insert(profileIndex);
        emit pinResult(true, {});
    } else {
        emit pinResult(false, QStringLiteral("Incorrect PIN"));
    }
}

void ProfileManager::rememberVerifiedPin(int profileIndex, const QString& pin)
{
    const auto target = profile(profileIndex);
    const QString salt = generateProfilePinSalt();
    const QJsonObject payload{
        {QStringLiteral("salt"), salt},
        {QStringLiteral("digest"),
         hashProfilePin(profileIndex, salt, pin)},
        {QStringLiteral("profileUpdatedAt"),
         target ? target->updatedAt : QString()},
    };
    ProfilePinCache::savePayload(
        profileIndex,
        QString::fromUtf8(
            QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void ProfileManager::pushAll(const QList<NuvioProfile>& profiles)
{
    QJsonArray arr;
    for (const NuvioProfile& p : profiles)
        arr.append(ProfileCodec::profileToPushJson(p));
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con](bool ok, int, const QJsonDocument&,
                               QByteArray) {
                       disconnect(*con);
                       if (ok) {
                           pullProfiles();
                           return;
                       }
                       emit operationResult(false,
                                            QStringLiteral("Network error"));
                   });
    m_client->call(QString::fromLatin1("sync_push_profiles"),
                   QJsonObject{
                       {QStringLiteral("p_client_max_profiles"), kMaxProfiles},
                       {QStringLiteral("p_profiles"), arr},
                       {QStringLiteral("p_origin_client_id"), originId()},
                   });
}

void ProfileManager::createProfile(const QString& name,
                                   const QString& avatarColorHex)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty()) {
        emit operationResult(false, QStringLiteral("Name required"));
        return;
    }
    QSet<int> used;
    for (const NuvioProfile& p : m_profiles) used.insert(p.profileIndex);
    int next = -1;
    for (int i = 1; i <= kMaxProfiles; ++i) {
        if (!used.contains(i)) {
            next = i;
            break;
        }
    }
    if (next < 0) {
        emit operationResult(false, QStringLiteral("Profile list is full"));
        return;
    }
    QList<NuvioProfile> all = m_profiles;
    NuvioProfile created;
    created.profileIndex = next;
    created.name = clean;
    created.avatarColorHex = avatarColorHex.isEmpty()
                                 ? QStringLiteral("#1E88E5")
                                 : avatarColorHex;
    created.userId = m_userId;
    all.append(created);
    if (!signedIn() || !m_cfg.valid()) {
        // Anonymous: local-only (Compose parity).
        m_profiles = all;
        std::sort(m_profiles.begin(), m_profiles.end(),
                  [](const NuvioProfile& a, const NuvioProfile& b) {
                      return a.profileIndex < b.profileIndex;
                  });
        persist();
        emit changed();
        emit operationResult(true, {});
        return;
    }
    pushAll(all);
}

void ProfileManager::renameProfile(int profileIndex, const QString& name)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty()) {
        emit operationResult(false, QStringLiteral("Name required"));
        return;
    }
    QList<NuvioProfile> all = m_profiles;
    bool found = false;
    for (NuvioProfile& p : all) {
        if (p.profileIndex == profileIndex) {
            p.name = clean;
            found = true;
        }
    }
    if (!found) {
        emit operationResult(false, QStringLiteral("Unknown profile"));
        return;
    }
    if (!signedIn() || !m_cfg.valid()) {
        m_profiles = all;
        persist();
        emit changed();
        emit operationResult(true, {});
        return;
    }
    pushAll(all);
}

void ProfileManager::setProfileColor(int profileIndex,
                                     const QString& avatarColorHex)
{
    QList<NuvioProfile> all = m_profiles;
    bool found = false;
    for (NuvioProfile& p : all) {
        if (p.profileIndex == profileIndex) {
            p.avatarColorHex = avatarColorHex;
            found = true;
        }
    }
    if (!found) {
        emit operationResult(false, QStringLiteral("Unknown profile"));
        return;
    }
    if (!signedIn() || !m_cfg.valid()) {
        m_profiles = all;
        persist();
        emit changed();
        emit operationResult(true, {});
        return;
    }
    pushAll(all);
}

void ProfileManager::deleteProfile(int profileIndex)
{
    bool found = false;
    for (const NuvioProfile& p : m_profiles) {
        if (p.profileIndex == profileIndex) found = true;
    }
    if (!found) {
        emit operationResult(false, QStringLiteral("Unknown profile"));
        return;
    }
    ProfilePinCache::removePayload(profileIndex);
    m_verifiedInSession.remove(profileIndex);
    if (!signedIn() || !m_cfg.valid()) {
        QList<NuvioProfile> remaining;
        for (const NuvioProfile& p : m_profiles) {
            if (p.profileIndex != profileIndex) remaining.append(p);
        }
        m_profiles = remaining;
        if (m_activeIndex == profileIndex) applyActive(1, false);
        else persist();
        emit changed();
        emit operationResult(true, {});
        return;
    }
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con, profileIndex](bool ok, int,
                                             const QJsonDocument&,
                                             QByteArray) {
                       disconnect(*con);
                       if (!ok) {
                           emit operationResult(
                               false, QStringLiteral("Network error"));
                           return;
                       }
                       QList<NuvioProfile> remaining;
                       for (const NuvioProfile& p : m_profiles) {
                           if (p.profileIndex != profileIndex)
                               remaining.append(p);
                       }
                       m_profiles = remaining;
                       if (m_activeIndex == profileIndex)
                           applyActive(1, false);
                       else
                           persist();
                       emit changed();
                       emit operationResult(true, {});
                   });
    // Server deletes the profile DATA; the row list refreshes via pull.
    // Compose deletes locally first for anonymous; signed-in deletes the
    // data server-side (sync_delete_profile_data), rows via next pull.
    m_client->call(QString::fromLatin1("sync_delete_profile_data"),
                   QJsonObject{
                       {QStringLiteral("p_profile_id"), profileIndex},
                       {QStringLiteral("p_origin_client_id"), originId()},
                   });
}

void ProfileManager::setPin(int profileIndex, const QString& pin,
                            const QString& currentPin)
{
    if (pin.isEmpty()) {
        emit operationResult(false, QStringLiteral("PIN required"));
        return;
    }
    if (!signedIn() || !m_cfg.valid()) {
        emit operationResult(false,
                             QStringLiteral("Sign in to manage PINs"));
        return;
    }
    QJsonObject params{
        {QStringLiteral("p_profile_id"), profileIndex},
        {QStringLiteral("p_pin"), pin},
    };
    if (!currentPin.isEmpty())
        params.insert(QStringLiteral("p_current_pin"), currentPin);
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con, profileIndex, pin](bool ok, int,
                                                  const QJsonDocument&,
                                                  QByteArray) {
                       disconnect(*con);
                       if (!ok) {
                           emit operationResult(
                               false, QStringLiteral("Network error"));
                           return;
                       }
                       rememberVerifiedPin(profileIndex, pin);
                       pullProfiles();
                       emit operationResult(true, {});
                   });
    m_client->call(QString::fromLatin1("set_profile_pin"), params);
}

void ProfileManager::clearPin(int profileIndex, const QString& currentPin)
{
    if (!signedIn() || !m_cfg.valid()) {
        emit operationResult(false,
                             QStringLiteral("Sign in to manage PINs"));
        return;
    }
    QJsonObject params{{QStringLiteral("p_profile_id"), profileIndex}};
    if (!currentPin.isEmpty())
        params.insert(QStringLiteral("p_current_pin"), currentPin);
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con, profileIndex](bool ok, int,
                                             const QJsonDocument&,
                                             QByteArray) {
                       disconnect(*con);
                       if (!ok) {
                           emit operationResult(
                               false, QStringLiteral("Network error"));
                           return;
                       }
                       ProfilePinCache::removePayload(profileIndex);
                       pullProfiles();
                       emit operationResult(true, {});
                   });
    m_client->call(QString::fromLatin1("clear_profile_pin"), params);
}

void ProfileManager::pullProfiles()
{
    if (!signedIn() || !m_cfg.valid()) {
        m_loaded = true;
        emit changed();
        return;
    }
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con](bool ok, int, const QJsonDocument& doc,
                               QByteArray) {
                       disconnect(*con);
                       if (!ok) {
                           m_loaded = true;
                           emit changed();
                           return;
                       }
                       QList<NuvioProfile> fetched;
                       const QJsonArray arr =
                           doc.isArray() ? doc.array() : QJsonArray{};
                       for (const QJsonValue& v : arr)
                           fetched.append(
                               ProfileCodec::profileFromJson(v.toObject()));
                       std::sort(fetched.begin(), fetched.end(),
                                 [](const NuvioProfile& a,
                                    const NuvioProfile& b) {
                                     return a.profileIndex < b.profileIndex;
                                 });
                       m_profiles = fetched;
                       // Active follows the stored index, else the first.
                       bool haveActive = false;
                       for (const NuvioProfile& p : m_profiles) {
                           if (p.profileIndex == m_activeIndex) {
                               haveActive = true;
                               break;
                           }
                       }
                       if (!haveActive && !m_profiles.isEmpty())
                           applyActive(m_profiles.first().profileIndex, false);
                       else
                           persist();
                       // Drop caches for vanished/unpinned profiles.
                       QSet<int> pinned;
                       for (const NuvioProfile& p : m_profiles) {
                           if (p.pinEnabled) pinned.insert(p.profileIndex);
                       }
                       for (int i = 1; i <= kMaxProfiles; ++i) {
                           if (!pinned.contains(i))
                               ProfilePinCache::removePayload(i);
                       }
                       m_loaded = true;
                       emit changed();
                   });
    m_client->call(QString::fromLatin1("sync_pull_profiles"), QJsonObject{});
}

void ProfileManager::pullLocks()
{
    if (!signedIn() || !m_cfg.valid()) return;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con](bool ok, int, const QJsonDocument& doc,
                               QByteArray) {
                       disconnect(*con);
                       if (!ok) return;
                       const QJsonArray arr =
                           doc.isArray() ? doc.array() : QJsonArray{};
                       m_lockedUntil.clear();
                       for (const QJsonValue& v : arr) {
                           const QJsonObject o = v.toObject();
                           const int idx = o.value(QStringLiteral(
                                                       "profile_index"))
                                               .toInt(-1);
                           if (idx < 1) continue;
                           m_lockedUntil.insert(
                               idx,
                               o.value(QStringLiteral("pin_locked_until"))
                                   .toString());
                       }
                       emit changed();
                   });
    m_client->call(QString::fromLatin1("sync_pull_profile_locks"),
                   QJsonObject{});
}

} // namespace nuvio::profiles
