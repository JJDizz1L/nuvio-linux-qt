#pragma once

// Home-catalog settings sync transport (Appendix A): standalone pull
// (sync_pull_home_catalog_settings) + debounced push
// (sync_push_home_catalog_settings, merged shared payload), mirroring
// Compose's HomeCatalogSettingsSyncService (500 ms debounce, skips before
// the initial pull completes, skips while a remote merge is applying,
// skips across user/profile switches).
//
// Server contract: platform "home_catalog_shared", params p_profile_id /
// p_platform / p_settings_json + origin client id; the pull answers a row
// array whose first row carries settings_json. An absent blob preserves
// local state; a remote with empty items applies the flags only; hero
// flags never cross the wire (local-only, preserved per key on apply).

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/SyncRpcClient.h"

namespace nuvio::library {
class HomeShelves;
}

namespace nuvio::authsync {

class HomeCatalogSyncController final : public QObject {
    Q_OBJECT

public:
    using TokenProvider = std::function<QByteArray()>;
    using UserIdProvider = std::function<QString()>;

    explicit HomeCatalogSyncController(AuthConfig cfg, TokenProvider token,
                                       UserIdProvider userId,
                                       library::HomeShelves* shelves,
                                       int profileId,
                                       QObject* parent = nullptr);
    ~HomeCatalogSyncController() override;

    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }
    /// Profile switches (P7): retargets all subsequent operations. A
    /// pending pull token belongs to the old profile, so pushes stay
    /// gated until the new profile's pull completes (service parity).
    void setProfileId(int id) { m_profileId = id; }

    void onLocalCatalogChanged();
    void pushNow();
    void pullNow();

signals:
    void pushFinished(bool ok);
    void pullFinished(bool ok, bool applied);

private:
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }
    [[nodiscard]] QString originId();
    [[nodiscard]] bool pullTokenReady() const
    {
        return m_pullCompleteUser == m_userId() &&
               m_pullCompleteProfile == m_profileId && !m_userId().isEmpty();
    }

    AuthConfig m_cfg;
    TokenProvider m_token;
    UserIdProvider m_userId;
    library::HomeShelves* m_shelves = nullptr;
    SyncRpcClient* m_client = nullptr;
    QTimer m_debounce;

    int m_profileId = 1;
    int m_inFlight = 0;
    bool m_applyRemote = false;
    QString m_pullCompleteUser;
    int m_pullCompleteProfile = -1;
    QJsonObject m_cachedRemote;   // last seen server settings_json
    bool m_haveCachedRemote = false;
};

} // namespace nuvio::authsync
