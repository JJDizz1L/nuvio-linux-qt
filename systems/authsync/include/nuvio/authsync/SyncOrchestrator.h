#pragma once

// Profile-settings sync orchestration (P4 leg 4): pull/apply + debounced
// push over SyncRpcClient, wired to AppSettings change signals.
//
// Semantics mirror Compose ProfileSettingsSync where applicable to a PARTIAL
// feature set (player_settings only today):
//   - pull:  p_profile_id/p_platform params; response is a JSON ARRAY whose
//            first row carries settings_json (blob v3). Applied ONLY when the
//            remote player fragment differs from our export; afterwards one
//            echo-suppression signature is armed (skipNext).
//   - push:  any AppSettings parity signal schedules a debounce; fire builds
//            blob {"version":3,"features":{"player_settings":...}} and skips
//            when identical to the last pushed payload. NEVER sends empty
//            feature maps for keys we don't manage (Compose wipes those).
//   - signed-out / unconfigured endpoints make every operation a no-op.
//
// Async throughout - never wait on the QML thread (gotcha #7).

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

namespace nuvio::authsync {

class SyncOrchestrator final : public QObject {
    Q_OBJECT

public:
    /// accessTokenProvider returns the user JWT (empty when signed out);
    /// wire it to AuthService::accessToken in production, a lambda in tests.
    using TokenProvider = std::function<QByteArray()>;

    explicit SyncOrchestrator(settings::AppSettings* settings,
                              AuthConfig cfg, TokenProvider token,
                              QObject* parent = nullptr);
    ~SyncOrchestrator() override;

    void setProfileId(int id) { m_profileId = id; }
    /// Test hook: shrink the push debounce window.
    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }

    /// One guarded async pull; harmless no-op when signed out/busy.
    void pullNow();
    /// Connects AppSettings change signals -> debounced push.
    void beginObserving();

signals:
    /// applied=true means the remote differed AND was merged locally.
    void pullFinished(bool applied);
    void pushFinished(bool ok);

private:
    void schedulePush();
    void doPush();
    [[nodiscard]] QByteArray currentExportSig();
    [[nodiscard]] QJsonObject baseParams();
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }

    settings::AppSettings* m_settings = nullptr;
    AuthConfig m_cfg;
    TokenProvider m_token;
    SyncRpcClient* m_client = nullptr;
    QTimer m_debounce;

    int  m_profileId       = 1;
    int  m_inFlight        = 0;      // 0 idle, else busy
    bool m_applyRemote     = false;  // suppress push while merging
    std::optional<QByteArray> m_lastPushSig;
    std::optional<QByteArray> m_skipNextSig;
};

} // namespace nuvio::authsync