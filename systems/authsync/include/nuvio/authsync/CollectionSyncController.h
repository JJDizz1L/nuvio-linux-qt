#pragma once

// Collections sync transport (P5): full pull (sync_pull_collections) +
// debounced full push (sync_push_collections, verbatim exportToJson),
// mirroring CollectionSyncService (500 ms debounce, skips while a remote
// merge is applying). Fresh CollectionStore per operation.

#include <QByteArray>
#include <QObject>
#include <QTimer>

#include <functional>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/SyncRpcClient.h"

namespace nuvio::authsync {

class CollectionSyncController final : public QObject {
    Q_OBJECT

public:
    using TokenProvider = std::function<QByteArray()>;

    explicit CollectionSyncController(AuthConfig cfg, TokenProvider token,
                                      int profileId, QObject* parent = nullptr);
    ~CollectionSyncController() override;

    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }
    /// Profile switches (P7): retargets all subsequent operations.
    void setProfileId(int id) { m_profileId = id; }

    void onLocalCollectionsChanged();
    void pushNow();
    void pullNow();

signals:
    void pushFinished(bool ok);
    void pullFinished(bool ok, bool applied);

private:
    [[nodiscard]] bool signedIn() const { return !m_token().isEmpty(); }
    [[nodiscard]] QString originId();

    AuthConfig m_cfg;
    TokenProvider m_token;
    SyncRpcClient* m_client = nullptr;
    QTimer m_debounce;

    int m_profileId = 1;
    int m_inFlight = 0;
    bool m_applyRemote = false;
};

} // namespace nuvio::authsync
