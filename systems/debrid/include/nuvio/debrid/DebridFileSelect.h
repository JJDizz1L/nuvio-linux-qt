#pragma once

// Torrent file selection (D2): verbatim port of Compose's Torbox +
// RealDebrid file selectors (playable filter, specific-name match,
// episode-pattern match, fileIdx fallbacks, largest-video fallback) over
// plain file rows. Premiumize resolves direct-download links without file
// picking (directdl per source). Pure + headless-tested.

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace nuvio::debrid {

struct TorrentFile {
    int id = -1;
    QString name;         // display name (already resolved per vendor)
    QString mimeType;     // Torbox only; empty for Real-Debrid
    qint64 size = 0;      // bytes (RD: bytes)
};

/// Playable-video predicate (Torbox: video/* mime or video extension;
/// Real-Debrid: video extension only). Case-insensitive, Compose parity.
[[nodiscard]] bool isPlayableVideo(const QString& displayName,
                                   const QString& mimeType);
[[nodiscard]] QStringList episodePatterns(int season, int episode);

/// Unified selection (both vendors share the rule chain; size field
/// differs only in name). `specificNames` = stream-declared filenames
/// (empty on our line today), `fileIdx` = explicit index override.
[[nodiscard]] std::optional<TorrentFile> selectTorrentFile(
    const QList<TorrentFile>& files, const QStringList& specificNames,
    int season, int episode, int fileIdx = -1);

} // namespace nuvio::debrid
