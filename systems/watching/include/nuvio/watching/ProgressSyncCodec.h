#pragma once

// Watch-progress wire codec for the Supabase sync RPC family (leg: sync
// breadth). Mirrors Compose SupabaseProgressSyncAdapter byte-shapes:
//
//   sync_push_watch_progress          {p_profile_id, p_entries[...],
//                                      p_origin_client_id}
//     entry: {content_id, content_type, video_id, season, episode,
//             position, duration, last_watched, progress_key}
//     (kotlinx encodeDefaults=true + explicitNulls default -> season/episode
//      are ALWAYS present, null when absent)
//   sync_delete_watch_progress        {p_profile_id, p_keys[...], p_origin_client_id}
//   sync_get_watch_progress_delta_cursor {p_profile_id}  -> bare JSON number
//   sync_pull_watch_progress_delta    {p_profile_id, p_since_event_id, p_limit}
//     -> [{event_id, operation, progress_key, content_id, content_type,
//          video_id, season, episode, position, duration, last_watched}]
//   sync_pull_watch_progress          {p_profile_id, p_since_last_watched?,
//                                      p_limit?}  -> same entry shape as push
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

#include "nuvio/watching/WatchProgress.h"

namespace nuvio::watching {

class ProgressSyncCodec final {
public:
    struct DeltaEvent {
        long long eventId = 0;
        std::string operation;        // "upsert" | "delete"
        std::string progressKey;
        std::string contentId;
        std::string contentType;
        std::string videoId;
        std::optional<int> season;
        std::optional<int> episode;
        long long position = 0;
        long long duration = 0;
        long long lastWatched = 0;
    };

    [[nodiscard]] static QJsonObject syncEntryJson(const WatchEntry& e);
    [[nodiscard]] static QJsonObject pushParams(
        int profileId, const std::vector<WatchEntry>& entries,
        const QString& originClientId);
    [[nodiscard]] static QJsonObject deleteParams(
        int profileId, const std::vector<std::string>& progressKeys,
        const QString& originClientId);
    [[nodiscard]] static QJsonObject cursorParams(int profileId);
    [[nodiscard]] static QJsonObject deltaPullParams(
        int profileId, long long sinceEventId, int limit);
    [[nodiscard]] static QJsonObject fullPullParams(int profileId);

    /// Cursor responses are a BARE JSON number body ("4200") — which
    /// QJsonDocument cannot represent — so callers pass the raw body.
    [[nodiscard]] static std::optional<long long> parseCursor(
        const QByteArray& rawBody);
    [[nodiscard]] static std::vector<DeltaEvent> decodeDeltas(
        const QJsonDocument& doc);
    /// Full-pull rows use the same entry shape as push.
    [[nodiscard]] static std::vector<WatchEntry> decodeRecords(
        const QJsonDocument& doc);
};

} // namespace nuvio::watching