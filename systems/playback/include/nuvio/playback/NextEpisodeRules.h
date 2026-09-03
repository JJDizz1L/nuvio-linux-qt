#pragma once

// Next-episode rules (P3a): verbatim port of Compose
// features/player/skip/PlayerNextEpisodeRules (resolveNextEpisode,
// shouldShowNextEpisodeCard with its 97..100% / 0..3.5min clamps and the
// outro-early-fire rule, hasAired date compare, OUTRO_SEGMENT_TYPES) plus
// the shared content-identity helpers (extractImdbId tt-pattern,
// splitCompositeId "tt:S:E") also used by the parental/skip legs.
// Pure + headless-tested; QML reaches it through NextEpisodeHelper.

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace nuvio::playback {

struct EpisodeRef {
    int season = -1;
    int episode = -1;
    QString id;     // composite "tt:S:E" (or bare id for movies — unused)
    QString name;
};

struct SkipSegment {
    double startSec = 0.0;
    double endSec = 0.0;
    QString type;       // intro|recap|outro|op|ed|mixed-op|mixed-ed|credits...
    QString provider;   // introdb | aniskip (completion-key + attribution)
};

/// Sorted S/E order, entry after (curSeason,curEpisode); nullopt when the
/// current episode is unknown or last. Movies (no S/E) never continue.
[[nodiscard]] std::optional<EpisodeRef> resolveNextEpisode(
    const QList<EpisodeRef>& videos, int curSeason, int curEpisode);

/// Card visibility at (positionMs,durationMs). Outro-aware: when outro
/// segments exist and end near the file end, fires at the earliest outro
/// start; otherwise fires at the user threshold. Thresholds clamp exactly
/// like Compose (percent 97..100, minutes 0..3.5).
[[nodiscard]] bool shouldShowNextEpisodeCard(
    qint64 positionMs, qint64 durationMs,
    const QList<SkipSegment>& intervals, const QString& thresholdMode,
    float thresholdPercent, float thresholdMinutesBeforeEnd);

/// YYYY-MM-DD aired check; unknown/unparseable counts as aired (Compose).
[[nodiscard]] bool hasAired(const QString& released, int todayYear,
                            int todayMonth, int todayDay);

/// First tt\d+ token, must start with "tt" (parental/skip parity).
[[nodiscard]] std::optional<QString> extractImdbId(const QString& value);

/// Splits composite series ids ("tt123:2:4", legacy forms unsupported here —
/// MetaService normalizes first) into parent/season/episode.
struct CompositeId {
    QString parent;
    int season = -1;
    int episode = -1;
    [[nodiscard]] bool isEpisode() const { return season >= 0 && episode >= 0; }
};
[[nodiscard]] CompositeId splitCompositeId(const QString& id);

} // namespace nuvio::playback
