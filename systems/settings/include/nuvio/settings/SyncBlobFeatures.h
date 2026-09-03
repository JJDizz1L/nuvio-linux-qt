#pragma once

// Blob-v3 feature map (P1b): the 13 serial names of Compose
// MobileProfileSettingsFeatures plus the read-modify-write passthrough rule.
//
// WHY PASSTHROUGH: every blob field carries a kotlinx default ({} / ""), and
// Compose's applyRemoteBlob writes them BLINDLY (whole-object replace for
// theme, savePayload("") for the string features). A partial Qt push stored
// VERBATIM server-side would therefore wipe a sibling Compose client's theme,
// poster/card styles, badges, debrid/tmdb/mdblist/trakt/notifications state
// the next time it pulls. The orchestrator avoids this by caching every
// received-but-unowned feature verbatim and re-sending it on push — safe
// under BOTH replace and merge server semantics. Rules enforced here:
//   - never fabricate defaults for unowned features (absent = omitted);
//   - never send "" or {} for a feature we did not receive (wipe vectors).

#include <QJsonObject>
#include <QString>

#include <memory>

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::settings {

namespace BlobFeature {
constexpr auto kVersion = 3;
constexpr auto kTheme = "theme_settings";
constexpr auto kPosterCardStyle = "poster_card_style_settings_payload";
constexpr auto kCardDepthStyle = "card_depth_style_settings_payload";
constexpr auto kPlayer = "player_settings";
constexpr auto kStreamBadges = "stream_badge_settings";
constexpr auto kDebrid = "debrid_settings";
constexpr auto kTmdb = "tmdb_settings";
constexpr auto kMdbList = "mdblist_settings";
constexpr auto kMetaScreen = "meta_screen_settings_payload";
constexpr auto kCollectionMobile = "collection_mobile_settings_payload";
constexpr auto kContinueWatching = "continue_watching_settings_payload";
constexpr auto kTraktSettings = "trakt_settings_payload";
constexpr auto kTraktComments = "trakt_comments_settings";
constexpr auto kNotifications = "notifications_settings";
} // namespace BlobFeature

/// Builds {"version":3,"features":{player + passthrough}} for push.
/// `passthrough` holds verbatim-received unowned features (objects, strings,
/// or the notifications map); entries are copied as-is, never defaulted.
[[nodiscard]] QJsonObject buildPushBlob(const QJsonObject& playerFragment,
                                        const QJsonObject& passthrough);

/// Qt-local persistence for received-but-unowned features (store
/// "sync_blob_passthrough", key "<feature>_<profileId>", value = compact
/// JSON of the feature value). Merge-only: features absent from a pull keep
/// their cached copy (a partial pull must never evict). Profile-suffixed:
/// blob pulls are per-profile, so a switch must never forward the old
/// profile's fragments.
class BlobPassthroughStore final {
public:
    explicit BlobPassthroughStore(int profileId = ActiveProfile::id());

    /// Profile switches (P7).
    void setProfileId(int profileId);

    /// All cached features as a {"name": value} map (empty when cold).
    [[nodiscard]] QJsonObject loadAll();
    /// Upserts every entry of `received` (verbatim QJsonValues).
    void mergeFromPull(const QJsonObject& received);

private:
    // Long-lived member (snapshot-at-construction gotcha: per-call instances
    // would clobber concurrent writes).
    std::unique_ptr<PropertiesStore> m_store;
    int m_profileId;
};

} // namespace nuvio::settings
