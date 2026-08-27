#pragma once

// Discord Rich Presence client (parity port of the Compose line's
// DiscordRpc.kt + DiscordPresenceManager.kt semantics).
//
// Wire contract (see AGENTS.md "Discord Rich Presence"):
//   * unix socket: $XDG_RUNTIME_DIR/discord-ipc-0 (+ vesktop/flatpak/snap
//     candidates), frame = int32-le opcode + int32-le length + utf8 json
//   * ops HANDSHAKE=0 FRAME=1 CLOSE=2 PING=3 PONG=4; SET_ACTIVITY needs a
//     nonce; unknown/dead sockets must be DETECTED (reader) and recovered
//     through backoff — a silently dead write path is how presence never
//     returns (the original Compose bug).
//
// Reconnect semantics (Compose parity): daemon-style reads on one socket;
// EOF/close marks it dead, backoff 1s→2s→…→60s with jitterless doubling;
// successful handshake resets it. PING answers PONG immediately.
//
// Manager semantics: 800ms coalescing debounce (latest wins — position
// updates arrive every ~500ms and NEVER restart work), content-key dedup,
// seek detection |startTs delta| > 4s rebuilds timestamps, paused drops the
// timestamps. All pure-decision parts are offline-testable.

#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <QString>

namespace nuvio::integrations {

// ---- pure codec / payload builders ------------------------------------------

namespace discord {

constexpr quint32 OpHandshake = 0;
constexpr quint32 OpFrame     = 1;
constexpr quint32 OpClose     = 2;
constexpr quint32 OpPing      = 3;
constexpr quint32 OpPong      = 4;

/// int32-le op | int32-le len | json. Guards absurd lengths (>64k) since a
/// corrupt length once bricked readers in the wild.
[[nodiscard]] QByteArray buildFrame(quint32 op, const QByteArray& json);

/// Parses exactly one frame from the front of buf. nullopt when incomplete.
/// On success, frameBytesOut (when given) receives bytes consumed so callers
/// can drain pipelined frames.
[[nodiscard]] std::optional<std::pair<quint32, QByteArray>>
parseFrame(const QByteArray& buf, int* consumedOut = nullptr);

/// SET_ACTIVITY envelope. startEpochSec/endEpochSec < 0 omit timestamps.
[[nodiscard]] QByteArray buildSetActivity(const QString& nonce,
                                          const QString& state,
                                          const QString& details,
                                          qint64 startEpochSec = -1,
                                          qint64 endEpochSec   = -1);

} // namespace discord

// ---- runtime manager ---------------------------------------------------------

class DiscordPresence final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
public:
    explicit DiscordPresence(QObject* parent = nullptr);
    ~DiscordPresence() override;

    void setClientId(const QString& id);

    /// Live-feed API. Elapsed/total are SECONDS within the current media;
    /// elapsed<0 means unknown (presence without progress). Paused clears
    /// timestamps but keeps text; empty title clears the activity entirely.
    void setTitle(const QString& title);
    void updateProgress(double elapsedSec, double totalSec, bool paused);

    [[nodiscard]] bool isConnected() const;

public slots:
    /// Async: probes socket candidates, handshakes, starts reading.
    Q_INVOKABLE void connectNow();
    /// Clear presence and drop the connection.
    void stop();

signals:
    void connectionChanged();

private slots:
    void scheduleConnect();
    void markDead();
    void flushPending();
    [[nodiscard]] QString contentKey() const;

private:
    QString m_clientId{QStringLiteral("1532796978973638830")};

    QLocalSocket* m_socket = nullptr;
    QTimer        m_reconnectTimer;   ///< single-shot backoff
    QTimer        m_debounceTimer;    ///< 800 ms coalescer
    int           m_backoffMs = 1000;

    // pending/presented activity state
    QString m_title;
    double  m_elapsed = -1, m_total = -1;
    bool    m_paused = false;
    qint64  m_lastStartEpoch = -1;
    QString m_lastKey;

    void sendFrame(const QByteArray& f);
};

} // namespace nuvio::integrations