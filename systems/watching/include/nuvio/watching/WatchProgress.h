#pragma once

// Library watch-state foundation (explore/qtqml queue item #2).
//
// Mirrors the Compose line's on-device resume + watched storage contract
// EXACTLY (no invented spellings) so the two builds are cross-readable:
//
//   resume store file: $XDG_CONFIG_HOME/nuvio-linux/watch_progress.properties
//   resume key:        watch_progress_<profileId>   (Compose default = 1)
//   resume value:      JSON StoredWatchProgressPayload {
//                          entries:[<WatchProgressEntry>...],
//                          lastSuccessfulPushEpochMs, deltaCursorEventId,
//                          deltaInitialized, dirtyProgressKeys:[...] }
//
//   watched store file: $XDG_CONFIG_HOME/nuvio-linux/watched.properties
//   watched key:        watched_<profileId>
//   watched value:      JSON StoredWatchedPayload { items:[<WatchedItem>...], ... }
//
// Kotlin serialization is camelCased (encodeDefaults=true, ignoreUnknownKeys=true);
// nullable fields are omitted when null. The codec here is decode-tolerant of
// missing/nullable fields, so files written by EITHER build round-trip cleanly.
//
// Pure policy below (see WatchProgress.cpp, unit-tested against Compose vectors):

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nuvio::watching {

inline constexpr float kCompletionThresholdFraction = 0.90f;   // 90 %
inline constexpr long  kResumeStoreThresholdMs      = 1'000L;  // shouldStoreProgress
inline constexpr int   kDefaultProfileId          = 1;        // Compose default
inline constexpr int   kContinueWatchingLimit       = 20;       // DefaultContinueWatchingLimit
inline constexpr const char* kLocalProgressSource   = "local";

/// Compose's WatchProgressEntry identity + resume key:
///   season & episode both present -> "<contentId>_s<season>e<episode>"
///   otherwise                    -> "<contentId>"
inline std::string buildProgressKey(const std::string& contentId,
                                    std::optional<int> season,
                                    std::optional<int> episode)
{
    if (season && episode)
        return contentId + "_s" + std::to_string(*season) +
               "e" + std::to_string(*episode);
    return contentId;
}

/// Compose's watchedKey:  "<type>:<id>:<seasonOr-1>:<episodeOr-1>"
inline std::string buildWatchedKey(const std::string& type,
                                   const std::string& id,
                                   std::optional<int> season,
                                   std::optional<int> episode)
{
    const int s = season.value_or(-1);
    const int e = episode.value_or(-1);
    return ((type + ":") + id + ":") +
           std::to_string(s) + ":" + std::to_string(e);
}

/// Compose's buildPlaybackVideoId (episode-aware playback identity).
inline std::string buildPlaybackVideoId(const std::string& parentMetaId,
                                        std::optional<int> season,
                                        std::optional<int> episode,
                                        const std::optional<std::string>& fallbackVideoId = std::nullopt)
{
    if (season && episode) {
        return parentMetaId + ":" + std::to_string(*season) + ":" + std::to_string(*episode);
    }
    if (fallbackVideoId && !fallbackVideoId->empty()) {
        return *fallbackVideoId;
    }
    return parentMetaId;
}

/// A watch-progress row. Nullable fields mirror Compose's `Int?`/`String?`.
struct WatchEntry {
    std::string contentType{};
    std::string parentMetaId{};
    std::string parentMetaType{};
    std::string videoId{};
    std::string title{};
    std::optional<std::string> logo;
    std::optional<std::string> poster;
    std::optional<std::string> background;
    std::optional<int> season;
    std::optional<int> episode;
    std::optional<std::string> episodeTitle;
    std::optional<std::string> episodeThumbnail;
    long long lastPositionMs = 0;
    long long durationMs     = 0;
    long long lastUpdatedEpochMs = 0;   // epoch millis
    std::optional<std::string> providerName;
    std::optional<std::string> providerAddonId;
    std::optional<std::string> lastStreamTitle;
    std::optional<std::string> lastStreamSubtitle;
    std::optional<std::string> pauseDescription;
    std::optional<std::string> lastSourceUrl;
    bool    isCompleted = false;
    std::optional<float> progressPercent;   // Compose Float? (0..100)
    std::string source = kLocalProgressSource;
    std::optional<std::string> trackingProviderId;
    std::optional<std::string> trackingProviderItemId;
    std::optional<std::string> trackingSourceUrl;
    std::optional<std::string> progressKey; // empty => resolved from contentId/season/ep

    bool isEpisode() const { return season.has_value() && episode.has_value(); }

    /// Compose resolvedProgressKey(): explicit progressKey, else
    /// buildProgressKey(parentMetaId, season, episode).
    std::string resolvedProgressKey() const;

    /// Compose progressFraction: explicit percent if present, else
    /// lastPositionMs/durationMs, else 0.
    float progressFraction() const;

    /// Compose isEffectivelyCompleted (90% + completed flag), the
    /// threshold that drives "watched" markers.
    bool isEffectivelyCompleted() const;

    bool isResumable() const { return !isEffectivelyCompleted(); }
};

struct WatchedItem {
    std::string type{};
    std::string id{};
    std::string name{};
    std::optional<std::string> poster;
    std::optional<std::string> releaseInfo;
    std::optional<int> season;
    std::optional<int> episode;
    std::optional<std::string> videoId;
    long long markedAtEpochMs = 0;
};

/// Compose's StoredWatchProgressPayload top-level shape (delta/sync fields
/// are round-tripped verbatim so cross-line sync state is preserved).
struct StoredProgressPayload {
    std::vector<WatchEntry> entries{};
    long long lastSuccessfulPushEpochMs = 0;
    long long deltaCursorEventId = 0;
    bool      deltaInitialized = false;
    std::vector<std::string> dirtyProgressKeys{};
};

struct StoredWatchedPayload {
    std::vector<WatchedItem> items{};
    std::vector<std::string> fullyWatchedSeriesKeys{};
    std::vector<std::string> expandedSiblingKeys{};
    long long lastSuccessfulPushEpochMs = 0;
    long long deltaCursorEventId = 0;
    bool      deltaInitialized = false;
    std::vector<std::string> dirtyWatchedKeys{};
};

/// Compose's newestByProgressKey + filter resumable + sort-desc-by-freshness +
/// limit. Returns the visible "continue watching" set, newest first.
std::vector<WatchEntry> continueWatchingSelection(std::vector<WatchEntry> entries,
                                                   int limit = kContinueWatchingLimit);

/// Strict-weak ordering mirroring watchProgressEntryFreshnessComparator
/// (lastUpdatedEpochMs primary, then a cascade of tie-breakers).
bool progressFreshnessLess(const WatchEntry& a, const WatchEntry& b);

} // namespace nuvio::watching
