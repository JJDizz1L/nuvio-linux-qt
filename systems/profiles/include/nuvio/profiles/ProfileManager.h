#pragma once

// Profile management (P7): local payload (profiles store, `profiles` key),
// server CRUD (sync_pull/push_profiles, sync_delete_profile_data,
// verify/set/clear_profile_pin, sync_pull_profile_locks), PIN verify
// (online RPC, offline salted cache), and switching (ActiveProfile +
// activeProfileChanged for the shell's per-object reload fan-out).
//
// PIN-gated switching rule: profiles with pinEnabled require a successful
// verifyPin in-session first (tracked per index); switchToProfile refuses
// otherwise. Lockouts (pin_locked_until in the future) refuse with the
// server message. Anonymous sessions work fully local (Compose parity).

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>
#include <optional>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/SyncRpcClient.h"
#include "nuvio/profiles/ProfileStore.h"

namespace nuvio::profiles {

class ProfileManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profilesVariant NOTIFY changed)
    Q_PROPERTY(int activeProfileIndex READ activeProfileIndex
                   NOTIFY changed)
    Q_PROPERTY(bool hasEverSelectedProfile READ hasEverSelectedProfile
                   NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)

public:
    using TokenProvider = std::function<QByteArray()>;

    explicit ProfileManager(nuvio::authsync::AuthConfig cfg, TokenProvider token,
                            QObject* parent = nullptr);

    [[nodiscard]] QVariantList profilesVariant() const;
    [[nodiscard]] int activeProfileIndex() const { return m_activeIndex; }
    [[nodiscard]] bool hasEverSelectedProfile() const
    {
        return m_hasEverSelected;
    }
    [[nodiscard]] bool loaded() const { return m_loaded; }
    [[nodiscard]] std::optional<NuvioProfile> profile(int index) const;

    /// Loads the local payload (call after auth restore; userId mismatches
    /// reset to a fresh payload). Does not touch the network.
    Q_INVOKABLE void loadLocal();
    /// Binds the payload to an account id (call on auth changes).
    Q_INVOKABLE void setAuthUserId(const QString& userId);

    /// Switches the active profile (PIN-gated, see class note). Emits
    /// activeProfileChanged on success for the shell reload fan-out.
    Q_INVOKABLE void switchToProfile(int profileIndex);
    /// Async PIN check (online RPC when signed in, salted cache offline).
    Q_INVOKABLE void verifyPin(int profileIndex, const QString& pin);
    Q_INVOKABLE void createProfile(const QString& name,
                                   const QString& avatarColorHex);
    Q_INVOKABLE void renameProfile(int profileIndex, const QString& name);
    Q_INVOKABLE void setProfileColor(int profileIndex,
                                     const QString& avatarColorHex);
    Q_INVOKABLE void deleteProfile(int profileIndex);
    Q_INVOKABLE void setPin(int profileIndex, const QString& pin,
                            const QString& currentPin = QString());
    Q_INVOKABLE void clearPin(int profileIndex,
                              const QString& currentPin = QString());
    Q_INVOKABLE void pullProfiles();
    Q_INVOKABLE void pullLocks();

signals:
    void changed();
    /// Shell hook: reload every profile-bound object for the new index.
    void activeProfileChanged(int profileIndex);
    void pinResult(bool unlocked, const QString& message);
    void operationResult(bool ok, const QString& message);

private:
    void persist();
    void applyActive(int profileIndex, bool markSelected);
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }
    [[nodiscard]] QString originId();
    void rememberVerifiedPin(int profileIndex, const QString& pin);
    /// Offline PIN-cache check: true = must go online (no cache).
    [[nodiscard]] bool pinNeedsOnline(int profileIndex) const;
    void pushAll(const QList<NuvioProfile>& profiles);

    nuvio::authsync::AuthConfig m_cfg;
    TokenProvider m_token;
    nuvio::authsync::SyncRpcClient* m_client = nullptr;

    QString m_userId;
    QList<NuvioProfile> m_profiles;
    int m_activeIndex = 1;
    bool m_hasEverSelected = false;
    bool m_rememberLastProfile = false;
    bool m_loaded = false;
    QSet<int> m_verifiedInSession;
    QHash<int, QString> m_lockedUntil;
    int m_inFlight = 0;
};

} // namespace nuvio::profiles
