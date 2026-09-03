#pragma once

// P1 playback-session wiring (the recorded next-queue item, done 2026-08-27).
//
// Bridges StreamResolver outcomes to navigation/playback:
//   requestPlay(type, imdbId, title) -> either playbackReady(title, url)
//   once a direct source exists, or playbackUnavailable(title) when every
//   configured addon answered WITHOUT one (torrent-only / unreachable /
//   none installed). Torrent entries stay skipped at the resolver level -
//   with the P2P engine offline they must never produce phantom playables.
//
// Two completion paths, both explicit:
//   * cache-hit: resolve() short-circuits when results already exist and
//     NO further resolutionComplete will ever fire - isComplete() catches
//     this and decides synchronously inside requestPlay (so repeat card
//     clicks relaunch instead of silently doing nothing),
//   * in-flight: matched on the resolver's resolutionComplete, with a
//     pending-key guard dropping late completions for superseded clicks.
//
// Direct sources launch immediately. When only a TORRENT entry exists and
// a P2pEngine is attached, the session hands the info-hash to the engine
// and relays its queued streamReady/streamFailed back onto this object's
// own terminal signals - callers need no P2P awareness at all.
//
// Offline-testable by construction: no QML, no GUI, no network here.

#include <QObject>
#include <QString>

#include <functional>

namespace nuvio::p2p { class P2pEngine; }

namespace nuvio::playback {

class StreamResolver;

/// Reuse-link policy (Compose StreamDestination parity): when enabled with
/// a positive cache window, requestPlay serves a fresh cached link instead
/// of resolving, and direct resolutions refresh the cache.
struct ReusePolicy {
    bool enabled = false;
    int cacheHours = 24;
};

class PlaybackSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY sessionChanged)
    Q_PROPERTY(QString currentUrl   READ currentUrl   NOTIFY sessionChanged)
    Q_PROPERTY(QString currentType  READ currentType  NOTIFY sessionChanged)
    Q_PROPERTY(QString currentId    READ currentId    NOTIFY sessionChanged)
    Q_PROPERTY(bool    hasSession   READ hasSession   NOTIFY sessionChanged)
    /// True when the current url is a transient localhost P2P relay (never
    /// handed to external players or the reuse cache).
    Q_PROPERTY(bool currentIsLocalRelay READ currentIsLocalRelay
                   NOTIFY sessionChanged)
    /// Parsed series position of the current session (-1 for movies).
    Q_PROPERTY(int currentSeason READ currentSeason NOTIFY sessionChanged)
    Q_PROPERTY(int currentEpisode READ currentEpisode NOTIFY sessionChanged)

public:
    explicit PlaybackSession(StreamResolver* resolver,
                             QObject* parent = nullptr);
    /// With an engine attached, torrent-only resolutions route through it
    /// instead of surfacing playbackUnavailable right away.
    PlaybackSession(StreamResolver* resolver, nuvio::p2p::P2pEngine* p2p,
                    QObject* parent = nullptr);

    using ReusePolicyProvider = std::function<ReusePolicy()>;
    /// Compose StreamDestination parity (P3a): consult before resolving.
    void setReusePolicyProvider(ReusePolicyProvider provider)
    {
        m_reuseProvider = std::move(provider);
    }

    /// User intent: play this item. Supersedes any earlier pending request.
    Q_INVOKABLE void requestPlay(const QString& type, const QString& imdbId,
                                 const QString& title = QString());

    [[nodiscard]] QString currentTitle() const { return m_title; }
    [[nodiscard]] QString currentUrl()   const { return m_url; }
    [[nodiscard]] QString currentType()  const { return m_type; }
    [[nodiscard]] QString currentId()    const { return m_id; }
    [[nodiscard]] bool    hasSession()   const { return !m_url.isEmpty(); }
    [[nodiscard]] int     currentSeason() const { return m_season; }
    [[nodiscard]] int     currentEpisode() const { return m_episode; }
    [[nodiscard]] bool    currentIsLocalRelay() const { return m_isRelay; }

signals:
    /// Direct source resolved - consumer navigates to the player route.
    void playbackReady(const QString& title, const QString& url);
    /// Honest negative: nothing directly playable (toast path).
    void playbackUnavailable(const QString& title);
    void sessionChanged();

private:
    /// Evaluate the CURRENTLY pending key against cached resolver results
    /// and emit exactly one terminal signal (possibly deferred through the
    /// P2P engine).
    void decide();

    StreamResolver* m_resolver = nullptr;
    nuvio::p2p::P2pEngine* m_p2p = nullptr;
    ReusePolicyProvider m_reuseProvider;
    quint64 m_activeToken = 0;
    bool    m_awaitingP2p = false;

    // Latest user intent (pending-key guard for stale completions).
    QString m_pendingType;
    QString m_pendingId;
    QString m_pendingTitle;

    // Last accepted session payload (drives QML properties).
    QString m_title;
    QString m_url;
    QString m_type;
    QString m_id;
    int m_season = -1;
    int m_episode = -1;
    bool m_isRelay = false;
};

} // namespace nuvio::playback