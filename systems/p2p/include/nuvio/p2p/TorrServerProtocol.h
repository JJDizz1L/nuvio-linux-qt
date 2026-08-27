#pragma once

// Pure TorrServer wire-contract + policy layer (parity port of the Compose
// line's P2pStreamingEngine.kt internals). Everything here is deterministic:
// no sockets, no processes - HTTP bodies are BUILT here, replies are PARSED
// here, and every policy decision (file-index precedence, settings merge)
// is a pure function over fixture data. That keeps the entire Stremio
// torrent-selection contract offline-testable, exactly like StreamResolver.
//
// Wire contract (verified against the shipped TorrServer releases):
//   POST /torrents  {action:add|get|drop, ...}  -> JSON object
//   GET  /echo                                    -> liveness probe
//   GET  /shutdown                                -> graceful exit
//   GET/POST /settings                            -> schema varies between
//     versions => ALWAYS fetch-modify-post, overriding ONLY "cache" (MB).

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace nuvio::p2p {

// ---- request bodies --------------------------------------------------------

QByteArray addTorrentRequestBody(const QString& magnetLink);   // action=add
QByteArray getTorrentRequestBody(const QString& hash);         // action=get
QByteArray dropTorrentRequestBody(const QString& hash);        // action=drop

// ---- parsing ----------------------------------------------------------------

struct TorrentFile {
    int     id          = 0;    // TorrServer ids are 1-BASED
    QString path;               // full path inside the torrent
    qint64  lengthBytes = 0;
};

struct TorrentStats {
    qint64 downloadSpeedBps = 0;
    qint64 uploadSpeedBps   = 0;
    int    peers            = 0;
    int    seeds            = 0;
    qint64 preloadedBytes   = 0;
    qint64 loadedSize       = 0;
    qint64 torrentSize      = 0;
    QList<TorrentFile> files;
};

/// Parses one /torrents action=get reply. nullopt when the body is not a
/// JSON object (callers must treat that as "stats unavailable", not fatal).
[[nodiscard]] std::optional<TorrentStats>
parseTorrentStats(const QByteArray& body);

// ---- policy -----------------------------------------------------------------

/// Canonical magnet URI: trimmed/lowercased info-hash validated as exactly
/// 40 (v1 btih) or 64 (v2 btmh-multihash) hex chars; blank/hashless trackers
/// filtered, distinct; per-component strict percent-encoding (RFC3986
/// unreserved kept, everything else %XX uppercase). Returns "" for invalid
/// hashes (C++ side has no exceptions; callers treat "" as user input error).
[[nodiscard]] QString buildMagnetUri(
    const QString& infoHash, const QStringList& extraTrackers = {});

/// Direct-playable HTTP URL for one file of the torrent through the local
/// engine ($base/stream?link=<encoded magnet>&index=<id>&play).
[[nodiscard]] QString buildStreamUrl(const QString& baseUrl,
                                     const QString& magnetLink,
                                     int fileIndex);

/// File-picker precedence chain (parity port, ORDER IS THE CONTRACT):
///   1. exact basename == filename (case-insensitive)
///   2. path contains filename (case-insensitive)
///   3. requestedIdx+1 names an existing TorrServer id (stremio offset)
///   4. requestedIdx indexes positionally into the file list
///   5. largest video-extension file
///   6. largest file of any type
///   7. 1
/// Caller guarantees: empty list falls through to 7.
[[nodiscard]] int resolveFileIndex(const QList<TorrentFile>& files,
                                   int requestedIdx /* -1 when absent */,
                                   const QString& filename /* may be empty */);

/// GET-modify-POST delta: parses the CURRENT /settings object and overrides
/// ONLY "cache" (RAM piece-cache MB), preserving every unknown field
/// verbatim (schema drift between versions is the reason this exists).
/// False when the current body does not parse (nothing is written out).
[[nodiscard]] bool mergeCacheSettings(const QByteArray& currentSettingsJson,
                                      int cacheMb,
                                      QByteArray* updatedOut);

} // namespace nuvio::p2p