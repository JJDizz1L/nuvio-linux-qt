#pragma once

// Slice-2 runtime: drives the pure kernel over the wire (slice-1 scope plus
// the fallback-key innertube path - watch-page config extraction and
// reachability probes are parked for slice 3 exactly as documented in the
// kernel header).
//
// Resolution walks the client chain in order; each answered player response
// is funneled through parseStreamingData and accumulated into shared,
// visionos-first ordered chains; buildPlaybackSource then applies the
// four-tier preference policy once, after every client has spoken.

#include <QObject>
#include <QString>

namespace nuvio::trailer {

class TrailerResolver final : public QObject {
    Q_OBJECT
public:
    explicit TrailerResolver(QObject* parent = nullptr);

    /// Accepts a bare video key or any recognizable YouTube URL form.
    Q_INVOKABLE void resolveForKey(const QString& keyOrUrl);

signals:
    void trailerResolved(const QString& url, const QString& audioUrl);
    void trailerFailed(const QString& reason);
};

} // namespace nuvio::trailer