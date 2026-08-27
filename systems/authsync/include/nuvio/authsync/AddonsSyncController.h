#pragma once

// Addons table sync (sync-breadth): full-state push + table pull.
//   push: rpc sync_push_addons {p_profile_id, p_addons[
//         {url,name,enabled,sort_order}], p_origin_client_id}
//         (server REPLACES that profile's rows -> removes/enables ride free)
//   pull: GET /rest/v1/addons?profile_id=eq.N&select=...&order=sort_order.asc
// Debounced on registry changes; hash-deduped against last-pushed state.

#include <QObject>
#include <QTimer>

#include <functional>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/SyncRpcClient.h"
#include "nuvio/library/AddonRegistry.h"

namespace nuvio::authsync {

class AddonsSyncController final : public QObject {
    Q_OBJECT

public:
    using TokenProvider = std::function<QByteArray()>;

    explicit AddonsSyncController(nuvio::library::AddonRegistry* registry,
                                  AuthConfig cfg, TokenProvider token,
                                  QObject* parent = nullptr);
    ~AddonsSyncController() override;

    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }
    void pullNow();
    void beginObserving();   // registry changes -> debounced push

signals:
    void pullFinished(bool ok, int applied);
    void pushFinished(bool ok);

private:
    void schedulePush();
    void doPush();
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }
    [[nodiscard]] QString originId();

    nuvio::library::AddonRegistry* m_registry = nullptr;
    AuthConfig m_cfg;
    TokenProvider m_token;
    SyncRpcClient* m_client = nullptr;
    QTimer m_debounce;

    int  m_inFlight = 0;
    bool m_lastPushValid = false;
    QByteArray m_lastPushSig;
};

} // namespace nuvio::authsync