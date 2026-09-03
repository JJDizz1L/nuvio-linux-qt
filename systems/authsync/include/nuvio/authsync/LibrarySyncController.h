#pragma once

// Library sync transport (P5): snapshot/delta/cursor/push/delete over
// SyncRpcClient, mirroring SupabaseLibrarySyncAdapter + the reconciler
// semantics (pending overlays, legacy-migration when the server is empty,
// delta skips pending keys, cursor = max event id). Fresh LibraryStore
// per operation (snapshot-at-construction rule). Signed-out/unconfigured
// endpoints are no-ops. Batch size 500 verbatim (LibrarySyncPaging).

#include <QByteArray>
#include <QObject>
#include <QTimer>

#include <functional>
#include <vector>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/SyncRpcClient.h"
#include "nuvio/library/LibraryStore.h"

namespace nuvio::authsync {

class LibrarySyncController final : public QObject {
    Q_OBJECT

public:
    using TokenProvider = std::function<QByteArray()>;

    explicit LibrarySyncController(AuthConfig cfg, TokenProvider token,
                                   int profileId, QObject* parent = nullptr);
    ~LibrarySyncController() override;

    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }
    /// Profile switches (P7): retargets all subsequent operations.
    void setProfileId(int id) { m_profileId = id; }

    /// Call whenever the local library changed; coalesced dirty push.
    void onLocalLibraryChanged();
    void pushDirty();
    /// Startup sequence: full snapshot merge + cursor seed, then deltas.
    void fullLibrarySyncThenDeltas();
    void pullLibraryDelta();

signals:
    void pushFinished(bool ok, int ops);
    void pullFinished(bool ok, int applied);

private:
    void fetchSnapshotPage(int offset);
    void finishSnapshotMerge();
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }
    [[nodiscard]] QString originId();

    AuthConfig m_cfg;
    TokenProvider m_token;
    SyncRpcClient* m_client = nullptr;
    QTimer m_debounce;

    int m_profileId = 1;
    int m_inFlight = 0;
    bool m_initialSyncDone = false;
    std::vector<nuvio::library::LibraryItem> m_snapAccum;
    qint64 m_deltaCursor = 0;
};

} // namespace nuvio::authsync
