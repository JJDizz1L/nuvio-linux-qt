#pragma once

// Profile-settings sync orchestration (P4 leg 4, P1b full-fidelity): pull /
// apply + debounced push over SyncRpcClient, wired to AppSettings change
// signals.
//
// Semantics mirror Compose ProfileSettingsSync where applicable:
//   - pull:  p_profile_id/p_platform params; response is a JSON ARRAY whose
//            first row carries settings_json (blob v3). The player fragment
//            applies through AppSettings (diff emits); the CW payload string
//            applies verbatim into the CW store (+ recorder reload); every
//            other received feature is cached verbatim in the passthrough
//            store (SyncBlobFeatures rule: never fabricate, never forward
//            ""/{} for unreceived features).
//   - push:  any AppSettings parity signal schedules a debounce; fire builds
//            blob {"version":3,"features":{player fresh + passthrough cache}}
//            so a partial Qt client never wipes sibling Compose state under
//            EITHER replace or merge server semantics. Skips when identical
//            to the last pushed/merged payload. Pushes are gated until the
//            first pull ATTEMPT completes (first-push-before-pull would drop
//            server-side features the cache has never seen).
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
#include "nuvio/settings/SyncBlobFeatures.h"

namespace nuvio::settings {
class AppSettings;
}

namespace nuvio::debrid {
class DebridSettings;
}

namespace nuvio::watching {
class WatchRecorder;
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

    void setProfileId(int id)
    {
        m_profileId = id;
        m_passthrough.setProfileId(id);
    }
    /// Test hook: shrink the push debounce window.
    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }
    /// Remote-apply target for the CW payload string (optional; main.cpp
    /// wires the live recorder, tests leave it null).
    void setWatchRecorder(watching::WatchRecorder* recorder)
    {
        m_recorder = recorder;
    }
    /// Debrid settings source for the blob's debrid_settings feature
    /// (optional; absent keeps the player-only + passthrough shape).
    void setDebridSettings(debrid::DebridSettings* debrid)
    {
        m_debrid = debrid;
    }

    /// One guarded async pull; harmless no-op when signed out/busy.
    void pullNow();
    /// Connects AppSettings change signals -> debounced push.
    void beginObserving();
    /// Schedules a debounced push (also wired to non-AppSettings blob
    /// owners such as DebridSettings and the CW recorder in main.cpp).
    void schedulePush();

signals:
    /// applied=true means the remote differed AND was merged locally.
    void pullFinished(bool applied);
    void pushFinished(bool ok);

private:
    void doPush();
    [[nodiscard]] QByteArray currentExportSig();
    [[nodiscard]] QJsonObject fullPushBlob();
    [[nodiscard]] QJsonObject baseParams();
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }

    settings::AppSettings* m_settings = nullptr;
    watching::WatchRecorder* m_recorder = nullptr;
    debrid::DebridSettings* m_debrid = nullptr;
    AuthConfig m_cfg;
    TokenProvider m_token;
    SyncRpcClient* m_client = nullptr;
    QTimer m_debounce;
    settings::BlobPassthroughStore m_passthrough;

    int  m_profileId       = 1;
    int  m_inFlight        = 0;      // 0 idle, else busy
    bool m_applyRemote     = false;  // suppress push while merging
    bool m_pullAttempted   = false;  // first-push-before-pull gate
    std::optional<QByteArray> m_lastPushSig;
    std::optional<QByteArray> m_skipNextSig;
};

} // namespace nuvio::authsync