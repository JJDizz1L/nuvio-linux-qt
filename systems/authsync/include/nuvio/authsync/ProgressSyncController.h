#pragma once

// Watch-progress transport (sync-breadth leg): pushes local dirty entries
// and pulls deltas/full snapshots through SyncRpcClient, using the
// Compose-parity WatchingStore as the single local truth. Semantics mirror
// SupabaseProgressSyncAdapter + WatchProgressRepository:
//
//   pushDirty(): dirty keys split into present entries (push RPC) and
//                absent keys (delete RPC); success clears those dirty marks
//                and stamps lastSuccessfulPushEpochMs.
//   fullSyncThenDeltas(): first run (deltaInitialized=false) does a FULL
//                pull merged newest-wins-remote, then initializes the cursor
//                from sync_get_watch_progress_delta_cursor; later runs pull
//                only the delta since the stored cursor.
//   Signed-out / unconfigured endpoints: every entry point is a no-op.
//
// A FRESH WatchingStore is constructed per operation on purpose — the store
// snapshots at construction, and WatchRecorder writes through its own
// instance; per-op instances guarantee read-after-write freshness.

#include <QByteArray>
#include <QObject>
#include <QTimer>

#include <functional>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/SyncRpcClient.h"
#include "nuvio/watching/WatchProgress.h"

namespace nuvio::authsync {

class ProgressSyncController final : public QObject {
    Q_OBJECT

public:
    using TokenProvider = std::function<QByteArray()>;

    explicit ProgressSyncController(AuthConfig cfg, TokenProvider token,
                                    int profileId, QObject* parent = nullptr);
    ~ProgressSyncController() override;

    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }
    /// Test hook: shrink the watched full-pull page size.
    void setWatchedPageSize(int n) { m_watchPageSize = n; }

    /// Call whenever local progress changed (recorder signals); coalesced.
    void onLocalProgressChanged();
    /// Push the current dirty set now (also the debounce target).
    void pushDirty();
    /// Startup sequence: delta/full pull-merge, then push local dirt.
    void fullSyncThenDeltas();

    /// Call whenever the local watched set changed; coalesced separately.
    void onWatchedChanged();
    /// Watched analogue: paged full pull / deltas + dirty push.
    void fullWatchedSyncThenDeltas();
    void pushWatchedDirty();

signals:
    void pushFinished(bool ok, int ops);
    void pullFinished(bool ok, int applied);

private:
    void runDeleteLeg(const std::vector<std::string>& keys);
    void fetchWatchedPage(int page);
    void finishWatchedInitialMerge();
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }
    [[nodiscard]] QString originId();

    AuthConfig m_cfg;
    TokenProvider m_token;
    SyncRpcClient* m_client = nullptr;
    QTimer m_debounce;
    QTimer m_watchedDebounce;

    int  m_profileId = 1;
    int  m_inFlight  = 0;
    int  m_watchPageSize = 200;
    bool m_initialSyncDone = false;
    bool m_initialWatchedSyncDone = false;
    std::vector<std::string> m_pendingDeletes;
    std::vector<nuvio::watching::WatchedItem> m_watchAccum;
};

} // namespace nuvio::authsync
