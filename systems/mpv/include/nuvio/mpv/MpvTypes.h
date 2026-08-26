// Shared value types crossing the mpv system boundary.
// Nothing libmpv-specific may appear here — this header is the system's
// stable public vocabulary (link-law: playback/ may include exactly this).
#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace nuvio::mpv {

/** Immutable observation cache mirroring the bridge-line snapshot contract. */
struct PlaybackSnapshot {
    double  positionSec      = -1.0;   ///< time-pos; <0 when unavailable/idle
    double  durationSec      = -1.0;
    double  speed            = 1.0;
    double  volume           = 100.0;
    double  demuxerCacheSec  = -1.0;
    bool    paused           = false;
    bool    eofReached       = false;
    bool    pausedForCache   = false;
    bool    seekable         = false;
    bool    fileLoaded       = false;  ///< FILE_LOADED latch (selection window)
    QString hwdecCurrent;              ///< decoded via..., e.g. vaapi-copy

    [[nodiscard]] bool hasMedia() const { return durationSec > 0.0 || positionSec >= 0.0; }
};

enum class TrackKind : quint8 { Audio, Video, Subtitle, Other };

struct TrackInfo {
    qint64   id     = 0;
    TrackKind kind  = TrackKind::Other;
    QString  title;
    QString  lang;
    QString  codec;
    bool     def    = false;
    bool     forced = false;
};

// ---- Documented cache-limit math, ported verbatim from the Compose bridge --
//
// demuxer-max-back-bytes must NOT mirror demuxer-max-bytes: mpv's back buffer
// is ADDITIONAL, so mirroring doubles the configured footprint (AGENTS.md,
// 2026-08-20 ballooning fix). clamp(setting/4, 8 MiB, 64 MiB).
[[nodiscard]] constexpr qint64 demuxerBackBufferBytes(qint64 forwardBytes)
{
    const qint64 quarter  = forwardBytes / 4;
    const qint64 floor8MiB  = 8ll  * 1024 * 1024;
    const qint64 cap64MiB   = 64ll * 1024 * 1024;
    return qBound(floor8MiB, quarter, cap64MiB);
}

} // namespace nuvio::mpv

Q_DECLARE_METATYPE(nuvio::mpv::PlaybackSnapshot)
Q_DECLARE_METATYPE(nuvio::mpv::TrackInfo)
Q_DECLARE_METATYPE(QVector<nuvio::mpv::TrackInfo>)
