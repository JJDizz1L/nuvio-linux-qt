#pragma once

// Trailer extraction kernel - parity port of the Compose line's
// InAppYouTubeExtractor.kt (2026-08-27 snapshot). This kernel stays PURE:
//   * YouTube innertube client table (visionos > android_vr > android > ios)
//   * player request body/header builders (fallback API key path)
//   * streamingData -> scored candidates (formats / adaptiveFormats / HLS)
//   * playback-source preference policy:
//       adaptive_separate > progressive > hls_last_resort >
//       adaptive_video_only (muted degenerate)
//   * googlevideo host-rotation candidate building (hostRotationCandidates)
// Network plumbing (visitor-data/watch-config fetch and reachability probes
// with mn-host rotation) lands in the RESOLVER (slice 3); the policy layer
// works on any player-response body handed to it, exactly like the Compose
// structure where InAppYouTubeExtractor funnels every wire result through
// these same decisions.

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <QList>
#include <QPair>
#include <optional>

namespace nuvio::trailer {

struct YouTubeClient {
    const char* key      = nullptr;
    const char* id       = nullptr;    // x-youtube-client-name
    const char* version  = nullptr;    // x-youtube-client-version
    QString     userAgent;
    QVariantMap context;
};

/// Chain order IS the contract (Compose priority order).
[[nodiscard]] const QVector<YouTubeClient>& clients();

[[nodiscard]] QString fallbackInnertubeApiKey();

// ---- request builders -------------------------------------------------------

[[nodiscard]] QString extractVideoId(const QString& urlOrId);
[[nodiscard]] QByteArray playerRequestBody(const YouTubeClient& client,
                                           const QString& videoId,
                                           const QString& visitorData = {});
/// name->value header set (content-type/origin/client-name/version/UA/
/// optional x-goog-visitor-id).
[[nodiscard]] QVector<QPair<QString, QString>> playerRequestHeaders(
    const YouTubeClient& client, const QString& visitorData = {});

// ---- candidate model ----------------------------------------------------------

enum class StreamCategory : quint8 { Progressive, VideoOnly, AudioOnly };

struct StreamCandidate {
    QString client;
    int     priority = 0;
    QString url;
    double  score    = 0;
    qint64  bitrate  = 0;
    QString mimeType;
    bool    hasN     = false;
    int     height   = 0;
    int     fps      = 0;
    QString ext;                      // mp4|webm|m4a|...
};

struct StreamingBuckets {
    QList<StreamCandidate> progressive;   // muxed (video+audio)
    QList<StreamCandidate> video;         // adaptive video-only
    QList<StreamCandidate> audio;         // adaptive audio-only
    QString                hlsManifestUrl;
};

/// Parses one innertube player response body. Empty buckets when the body
/// is malformed or carries no streams - callers treat that as "this client
/// produced nothing", never fatal.
[[nodiscard]] StreamingBuckets parseStreamingData(const QByteArray& body);

// ---- ordering + source policy ---------------------------------------------

[[nodiscard]] int containerPreference(const QString& ext);

/// score desc, then no-n preferred, then container pref, then client prio.
[[nodiscard]] QList<StreamCandidate> sortCandidates(
    const QList<StreamCandidate>& items);

/// visionos-first partition, each half sorted (Compose orderSeparate).
[[nodiscard]] QList<StreamCandidate> orderSeparate(
    const QList<StreamCandidate>& items);

// ---- googlevideo host rotation ----------------------------------------------

/// Ordered reachability candidates for a googlevideo URL: the original URL
/// first, then one alternate per `mn` query-param server (rrN--- prefix or
/// sn- token replacement, Compose parity). Non-googlevideo URLs or ones
/// without alternates produce just [url]. Network probing itself lives in
/// the resolver; this stays a pure URL transform for offline tests.
[[nodiscard]] QStringList hostRotationCandidates(const QString& url);

struct PlaybackSource {
    QString mode;        // adaptive_separate|progressive|hls_last_resort|
                         // adaptive_video_only
    QString videoUrl;
    QString audioUrl;    // empty unless adaptive_separate
};

/// Preference policy WITHOUT reachability probing (slice-1 shape): takes
/// the ordered chains and picks the highest tier available.
[[nodiscard]] std::optional<PlaybackSource> buildPlaybackSource(
    const QList<StreamCandidate>& orderedProgressive,
    const QList<StreamCandidate>& orderedVideo,
    const QList<StreamCandidate>& orderedAudio,
    const QString& hlsManifestUrl);

} // namespace nuvio::trailer