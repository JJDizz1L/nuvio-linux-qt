#pragma once

// Provider-credential sync (T4): the Qt-owned API credentials
// (animeskip client_id, introdb api_key, tmdb api_key, mdblist api_key)
// through the Compose credential family
// (sync_push/seed/pull_provider_credentials), mirroring
// ProviderCredentialSync for our key subset (debrid has no Qt feature
// yet). Remote wins per provider; empty server + local values seeds;
// pushes fire only on observed change (in-memory baseline). The
// settings-blob export strips all credential keys (credential policy);
// this family is their only wire path.

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/SyncRpcClient.h"

namespace nuvio::settings {
class AppSettings;
}

namespace nuvio::tmdb {
class TmdbSettings;
}

namespace nuvio::mdblist {
class MdbListSettings;
}

namespace nuvio::authsync {

class ProviderCredsController final : public QObject {
    Q_OBJECT

public:
    using TokenProvider = std::function<QByteArray()>;

    explicit ProviderCredsController(settings::AppSettings* settings,
                                     tmdb::TmdbSettings* tmdb,
                                     mdblist::MdbListSettings* mdblist,
                                     AuthConfig cfg, TokenProvider token,
                                     int profileId,
                                     QObject* parent = nullptr);
    ~ProviderCredsController() override;

    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }
    /// Profile switches (P7).
    void setProfileId(int id) { m_profileId = id; }

    /// Call on credential-bearing settings changes; coalesced.
    void onLocalCredsChanged();
    /// Full cycle: push-if-dirty, pull, seed-or-merge, apply.
    void syncNow();

signals:
    void syncFinished(bool ok, bool applied);

private:
    [[nodiscard]] QJsonObject localSnapshot() const;
    void pushSnapshot(const QJsonObject& snapshot);
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }
    [[nodiscard]] QString originId();

    settings::AppSettings* m_settings = nullptr;
    tmdb::TmdbSettings* m_tmdb = nullptr;
    mdblist::MdbListSettings* m_mdbList = nullptr;
    AuthConfig m_cfg;
    TokenProvider m_token;
    SyncRpcClient* m_client = nullptr;
    QTimer m_debounce;

    int m_profileId = 1;
    int m_inFlight = 0;
    bool m_hasBaseline = false;
    QJsonObject m_baseline;   // last synced {provider: value}
};

} // namespace nuvio::authsync
