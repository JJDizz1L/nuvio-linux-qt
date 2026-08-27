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

namespace nuvio::p2p { class P2pEngine; }

namespace nuvio::playback {

class StreamResolver;

class PlaybackSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY sessionChanged)
    Q_PROPERTY(QString currentUrl   READ currentUrl   NOTIFY sessionChanged)
    Q_PROPERTY(bool    hasSession   READ hasSession   NOTIFY sessionChanged)

public:
    explicit PlaybackSession(StreamResolver* resolver,
                             QObject* parent = nullptr);
    /// With an engine attached, torrent-only resolutions route through it
    /// instead of surfacing playbackUnavailable right away.
    PlaybackSession(StreamResolver* resolver, nuvio::p2p::P2pEngine* p2p,
                    QObject* parent = nullptr);

    /// User intent: play this item. Supersedes any earlier pending request.
    Q_INVOKABLE void requestPlay(const QString& type, const QString& imdbId,
                                 const QString& title = QString());

    [[nodiscard]] QString currentTitle() const { return m_title; }
    [[nodiscard]] QString currentUrl()   const { return m_url; }
    [[nodiscard]] bool    hasSession()   const { return !m_url.isEmpty(); }

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
    quint64 m_activeToken = 0;
    bool    m_awaitingP2p = false;

    // Latest user intent (pending-key guard for stale completions).
    QString m_pendingType;
    QString m_pendingId;
    QString m_pendingTitle;

    // Last accepted session payload (drives QML properties).
    QString m_title;
    QString m_url;
};

} // namespace nuvio::playback