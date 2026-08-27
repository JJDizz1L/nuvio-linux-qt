#pragma once

// Slice-3 runtime: drives the pure kernel over the wire.
//   * visitor-data/watch-config fetch: one ANDROID player-API call harvests
//     the session visitor token (responseContext) and threads it into every
//     request body/header for the session - never fatal on failure.
//   * host-rotation reachability probing: the chosen googlevideo URLs (video
//     + optional separate audio) are Range-probed across their `mn` alternates
//     and the first reachable one is what gets played.
//
// Resolution walks the client chain in order; each answered player response
// is funneled through parseStreamingData and accumulated into shared,
// visionos-first ordered chains; buildPlaybackSource then applies the
// four-tier preference policy once, after every client has spoken.

#include <QObject>
#include <QString>

#include <mutex>

class QNetworkAccessManager;

namespace nuvio::trailer {

class TrailerResolver final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool resolving READ isResolving NOTIFY resolvingChanged)
public:
    explicit TrailerResolver(QObject* parent = nullptr);

    /// Accepts a bare video key or any recognizable YouTube URL form.
    /// Returns immediately; results arrive via trailerResolved/trailerFailed
    /// (resolution runs on a worker thread so the QML thread never blocks).
    Q_INVOKABLE void resolveForKey(const QString& keyOrUrl);
    /// Ambient (hero) variant: identical pipeline, but completion emits
    /// ambientResolved/ambientFailed so the shell's playback-route hijack
    /// never fires for background autoplays.
    Q_INVOKABLE void resolveForKeyAmbient(const QString& keyOrUrl);
    /// Preview (poster-hover) variant: completion emits previewResolved /
    /// previewFailed. Mutually exclusive with the other two modes per call.
    Q_INVOKABLE void resolveForKeyPreview(const QString& keyOrUrl);

    [[nodiscard]] bool isResolving() const { return m_resolving; }

signals:
    void trailerResolved(const QString& url, const QString& audioUrl);
    void trailerFailed(const QString& reason);
    void ambientResolved(const QString& url, const QString& audioUrl);
    void ambientFailed(const QString& reason);
    void previewResolved(const QString& url, const QString& audioUrl);
    void previewFailed(const QString& reason);
    void resolvingChanged();

private:
    void beginResolve(const QString& keyOrUrl);
    /// Runs on the worker thread: the full synchronous walk (visitor-data
    /// fetch -> client chain -> source policy -> host-rotation probes) builds
    /// locals, then QMetaObject::invokeMethod(QueuedConnection) delivers the
    /// terminal signal + m_resolving=false back on the QML thread.
    void runResolveWorker(const QString& videoId);

    /// Session-wide visitor token (one ANDROID player call, cached). Threads
    /// into every request body/header once present; empty on failure (the
    /// fallback API key alone still works, so this is never fatal).
    QString fetchVisitorData(QNetworkAccessManager& nam,
                             const QString& videoId);

    /// Walk host-rotation candidates in order and return the first that
    /// serves a Range probe (sequential-first-success). Non-googlevideo URLs
    /// pass through.
    QString resolveReachableUrlOrNull(QNetworkAccessManager& nam,
                                      const QString& url);
    bool isUrlReachable(QNetworkAccessManager& nam, const QString& url);

    bool m_resolving = false;        // read/written on the QML thread only
    // Mode of the in-flight resolution: routes the terminal signal.
    enum class Mode { Playback, Ambient, Preview };
    Mode m_mode = Mode::Playback;
    mutable std::mutex m_cacheMutex; // guards m_visitorData (worker-written)
    QString m_visitorData;
};

} // namespace nuvio::trailer