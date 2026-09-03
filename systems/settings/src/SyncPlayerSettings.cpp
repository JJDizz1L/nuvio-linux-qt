#include "nuvio/settings/SyncPlayerSettings.h"

#include <algorithm>
#include <vector>

#include <QJsonObject>
#include <QSet>
#include <QString>

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncPreferenceJson.h"

namespace nuvio::settings {

namespace {
// PropertiesStore keys are std::string_view-based (Java properties).
[[nodiscard]] std::string scoped(const char* key)
{
    return std::string(key) + "_" +
           std::to_string(ActiveProfile::id());
}

[[nodiscard]] QString scopedKey(const char* key)
{
    return QString::fromStdString(scoped(key));
}

// Compose PlayerSettingsStorage key names (unscoped forms), verbatim from
// PlayerSettingsStorage.desktop.kt. android_*/ios_* intentionally absent:
// mobile-only keys are never stored on this line, so present-only export
// omits them and merge-apply ignores them (contract-safe both ways).
constexpr auto kShowLoadingOverlay = "show_loading_overlay";
constexpr auto kShowParentalGuide = "show_parental_guide";
constexpr auto kResizeMode = "resize_mode";
constexpr auto kHoldToSpeedEnabled = "hold_to_speed_enabled";
constexpr auto kHoldToSpeedValue = "hold_to_speed_value";
constexpr auto kTouchGesturesEnabled = "touch_gestures_enabled";
constexpr auto kExternalPlayerEnabled = "external_player_enabled";
constexpr auto kExternalPlayerForwardSubtitles =
    "external_player_forward_subtitles";
constexpr auto kExternalPlayerSendSkipSegments =
    "external_player_send_skip_segments";
constexpr auto kExternalPlayerId = "external_player_id";
constexpr auto kPreferredAudioLang = "preferred_audio_language";
constexpr auto kSecondaryAudioLang = "secondary_preferred_audio_language";
constexpr auto kPreferredSubLang = "preferred_subtitle_language";
constexpr auto kSecondarySubLang = "secondary_preferred_subtitle_language";
constexpr auto kSubTextColor = "subtitle_text_color";
constexpr auto kSubBgColor = "subtitle_background_color";
constexpr auto kSubOutlineColor = "subtitle_outline_color";
constexpr auto kSubOutlineEnabled = "subtitle_outline_enabled";
constexpr auto kSubOutlineWidth = "subtitle_outline_width";
constexpr auto kSubBold = "subtitle_bold";
constexpr auto kSubFontSize = "subtitle_font_size_sp";
constexpr auto kSubBottomOffset = "subtitle_bottom_offset";
constexpr auto kSubStripSdh = "subtitle_strip_sdh";
constexpr auto kForcedSubtitles = "subtitle_use_forced_subtitles";
constexpr auto kSubShowOnlyPreferred = "subtitle_show_only_preferred_languages";
constexpr auto kAddonSubStartup = "addon_subtitle_startup_mode";
constexpr auto kReuseLastLink = "stream_reuse_last_link_enabled";
constexpr auto kReuseLastLinkHours = "stream_reuse_last_link_cache_hours";
constexpr auto kStreamCacheSize = "stream_cache_size";
constexpr auto kDecoderPriority = "decoder_priority";
constexpr auto kMapDv7ToHevc = "map_dv7_to_hevc";
constexpr auto kTunneling = "tunneling_enabled";
constexpr auto kApMode = "stream_auto_play_mode";
constexpr auto kApSource = "stream_auto_play_source";
constexpr auto kApAddons = "stream_auto_play_selected_addons";
constexpr auto kApPlugins = "stream_auto_play_selected_plugins";
constexpr auto kApRegex = "stream_auto_play_regex";
constexpr auto kApTimeout = "stream_auto_play_timeout_seconds";
constexpr auto kSkipIntro = "skip_intro_enabled";
constexpr auto kAutoSkipTypes = "auto_skip_segment_types";
constexpr auto kAnimeSkip = "animeskip_enabled";
constexpr auto kAnimeSkipClient = "animeskip_client_id";
constexpr auto kIntroDbKey = "introdb_api_key";   // credential: never exported
constexpr auto kIntroSubmit = "intro_submit_enabled";
constexpr auto kNextEp = "stream_auto_play_next_episode_enabled";
constexpr auto kNextEpFallback = "stream_auto_play_next_episode_fallback_enabled";
constexpr auto kPreferBinge = "stream_auto_play_prefer_binge_group";
constexpr auto kReuseBinge = "stream_auto_play_reuse_binge_group";
constexpr auto kThresholdMode = "next_episode_threshold_mode";
constexpr auto kThresholdPct = "next_episode_threshold_percent_v2";
constexpr auto kThresholdMin = "next_episode_threshold_minutes_before_end_v2";
constexpr auto kUseLibass = "use_libass";
constexpr auto kLibassRender = "libass_render_type";
constexpr auto kNvidiaRtx = "nvidia_rtx_super_resolution_enabled";

// Typed present-only export helpers (envelope-wrapped, kotlinx-shaped).
void putString(nuvio::settings::PropertiesStore& store, QJsonObject& out,
               const char* key)
{
    if (const auto v = store.getString(scoped(key)))
        out.insert(scopedKey(key), SyncPreferenceJson::encodeString(
                                       QString::fromStdString(*v)));
}

void putBoolean(nuvio::settings::PropertiesStore& store, QJsonObject& out,
                const char* key)
{
    if (const auto v = store.getBoolean(scoped(key)))
        out.insert(scopedKey(key), SyncPreferenceJson::encodeBoolean(*v));
}

void putInt(nuvio::settings::PropertiesStore& store, QJsonObject& out,
            const char* key)
{
    if (const auto v = store.getInt(scoped(key)))
        out.insert(scopedKey(key), SyncPreferenceJson::encodeInt(*v));
}

void putFloat(nuvio::settings::PropertiesStore& store, QJsonObject& out,
              const char* key)
{
    if (const auto v = store.getFloat(scoped(key)))
        out.insert(scopedKey(key), SyncPreferenceJson::encodeFloat(*v));
}

void putStringSet(nuvio::settings::PropertiesStore& store, QJsonObject& out,
                  const char* key)
{
    if (const auto v = store.getStringSet(scoped(key))) {
        QSet<QString> set;
        for (const auto& s : *v) set.insert(QString::fromStdString(s));
        out.insert(scopedKey(key), SyncPreferenceJson::encodeStringSet(set));
    }
}

bool takeString(nuvio::settings::PropertiesStore& store,
                const QJsonObject& payload, const char* key)
{
    const auto v =
        SyncPreferenceJson::decodeString(payload, scopedKey(key));
    if (!v) return false;
    store.putString(scoped(key), v->toStdString());
    return true;
}

bool takeBoolean(nuvio::settings::PropertiesStore& store,
                 const QJsonObject& payload, const char* key)
{
    const auto v =
        SyncPreferenceJson::decodeBoolean(payload, scopedKey(key));
    if (!v) return false;
    store.putBoolean(scoped(key), *v);
    return true;
}

bool takeInt(nuvio::settings::PropertiesStore& store,
             const QJsonObject& payload, const char* key)
{
    const auto v = SyncPreferenceJson::decodeInt(payload, scopedKey(key));
    if (!v) return false;
    store.putInt(scoped(key), *v);
    return true;
}

bool takeFloat(nuvio::settings::PropertiesStore& store,
               const QJsonObject& payload, const char* key)
{
    const auto v = SyncPreferenceJson::decodeFloat(payload, scopedKey(key));
    if (!v) return false;
    store.putFloat(scoped(key), *v);
    return true;
}

bool takeStringSet(nuvio::settings::PropertiesStore& store,
                   const QJsonObject& payload, const char* key)
{
    const auto v =
        SyncPreferenceJson::decodeStringSet(payload, scopedKey(key));
    if (!v) return false;
    std::vector<std::string> sorted;
    for (const auto& q : *v) sorted.push_back(q.toStdString());
    std::sort(sorted.begin(), sorted.end());
    store.putStringSet(scoped(key), sorted);
    return true;
}
} // namespace

QJsonObject PlayerSettingsSync::exportSyncPayload(
    nuvio::settings::PropertiesStore& playerStore)
{
    QJsonObject out;

    putBoolean(playerStore, out, kShowLoadingOverlay);
    putBoolean(playerStore, out, kShowParentalGuide);
    putString(playerStore, out, kResizeMode);
    putBoolean(playerStore, out, kHoldToSpeedEnabled);
    putFloat(playerStore, out, kHoldToSpeedValue);
    putBoolean(playerStore, out, kTouchGesturesEnabled);
    putBoolean(playerStore, out, kExternalPlayerEnabled);
    putBoolean(playerStore, out, kExternalPlayerForwardSubtitles);
    putBoolean(playerStore, out, kExternalPlayerSendSkipSegments);
    putString(playerStore, out, kExternalPlayerId);
    putString(playerStore, out, kPreferredAudioLang);
    putString(playerStore, out, kSecondaryAudioLang);
    putString(playerStore, out, kPreferredSubLang);
    putString(playerStore, out, kSecondarySubLang);
    putString(playerStore, out, kSubTextColor);
    putString(playerStore, out, kSubBgColor);
    putString(playerStore, out, kSubOutlineColor);
    putBoolean(playerStore, out, kSubOutlineEnabled);
    putInt(playerStore, out, kSubOutlineWidth);
    putBoolean(playerStore, out, kSubBold);
    putInt(playerStore, out, kSubFontSize);
    putInt(playerStore, out, kSubBottomOffset);
    putBoolean(playerStore, out, kSubStripSdh);
    putBoolean(playerStore, out, kForcedSubtitles);
    putBoolean(playerStore, out, kSubShowOnlyPreferred);
    putString(playerStore, out, kAddonSubStartup);
    putBoolean(playerStore, out, kReuseLastLink);
    putInt(playerStore, out, kReuseLastLinkHours);
    putString(playerStore, out, kStreamCacheSize);
    putInt(playerStore, out, kDecoderPriority);
    putBoolean(playerStore, out, kMapDv7ToHevc);
    putBoolean(playerStore, out, kTunneling);
    putString(playerStore, out, kApMode);
    putString(playerStore, out, kApSource);
    putStringSet(playerStore, out, kApAddons);
    putStringSet(playerStore, out, kApPlugins);
    putString(playerStore, out, kApRegex);
    putInt(playerStore, out, kApTimeout);
    putBoolean(playerStore, out, kSkipIntro);
    putStringSet(playerStore, out, kAutoSkipTypes);
    putBoolean(playerStore, out, kAnimeSkip);
    // kAnimeSkipClient + kIntroDbKey deliberately omitted: credentials
    // (Compose ProfileSettingsCredentialPolicy strips both from the blob;
    // they sync through the provider-credentials family instead).
    putBoolean(playerStore, out, kIntroSubmit);
    putBoolean(playerStore, out, kNextEp);
    putBoolean(playerStore, out, kNextEpFallback);
    putBoolean(playerStore, out, kPreferBinge);
    putBoolean(playerStore, out, kReuseBinge);
    putString(playerStore, out, kThresholdMode);
    putFloat(playerStore, out, kThresholdPct);
    putFloat(playerStore, out, kThresholdMin);
    putBoolean(playerStore, out, kUseLibass);
    putString(playerStore, out, kLibassRender);
    putBoolean(playerStore, out, kNvidiaRtx);

    return out;
}

bool PlayerSettingsSync::applyRemotePayload(
    nuvio::settings::PropertiesStore& playerStore, const QJsonObject& payload)
{
    bool touched = false;

    // Per-key optional-let semantics: invalid/absent -> untouched.
    touched |= takeBoolean(playerStore, payload, kShowLoadingOverlay);
    touched |= takeBoolean(playerStore, payload, kShowParentalGuide);
    touched |= takeString(playerStore, payload, kResizeMode);
    touched |= takeBoolean(playerStore, payload, kHoldToSpeedEnabled);
    touched |= takeFloat(playerStore, payload, kHoldToSpeedValue);
    touched |= takeBoolean(playerStore, payload, kTouchGesturesEnabled);
    touched |= takeBoolean(playerStore, payload, kExternalPlayerEnabled);
    touched |= takeBoolean(playerStore, payload, kExternalPlayerForwardSubtitles);
    touched |= takeBoolean(playerStore, payload, kExternalPlayerSendSkipSegments);
    touched |= takeString(playerStore, payload, kExternalPlayerId);
    touched |= takeString(playerStore, payload, kPreferredAudioLang);
    touched |= takeString(playerStore, payload, kSecondaryAudioLang);
    touched |= takeString(playerStore, payload, kPreferredSubLang);
    touched |= takeString(playerStore, payload, kSecondarySubLang);
    touched |= takeString(playerStore, payload, kSubTextColor);
    touched |= takeString(playerStore, payload, kSubBgColor);
    touched |= takeString(playerStore, payload, kSubOutlineColor);
    touched |= takeBoolean(playerStore, payload, kSubOutlineEnabled);
    touched |= takeInt(playerStore, payload, kSubOutlineWidth);
    touched |= takeBoolean(playerStore, payload, kSubBold);
    touched |= takeInt(playerStore, payload, kSubFontSize);
    touched |= takeInt(playerStore, payload, kSubBottomOffset);
    touched |= takeBoolean(playerStore, payload, kSubStripSdh);
    touched |= takeBoolean(playerStore, payload, kForcedSubtitles);
    touched |= takeBoolean(playerStore, payload, kSubShowOnlyPreferred);
    touched |= takeString(playerStore, payload, kAddonSubStartup);
    touched |= takeBoolean(playerStore, payload, kReuseLastLink);
    touched |= takeInt(playerStore, payload, kReuseLastLinkHours);
    touched |= takeString(playerStore, payload, kStreamCacheSize);
    touched |= takeInt(playerStore, payload, kDecoderPriority);
    touched |= takeBoolean(playerStore, payload, kMapDv7ToHevc);
    touched |= takeBoolean(playerStore, payload, kTunneling);
    touched |= takeString(playerStore, payload, kApMode);
    touched |= takeString(playerStore, payload, kApSource);
    touched |= takeStringSet(playerStore, payload, kApAddons);
    touched |= takeStringSet(playerStore, payload, kApPlugins);
    touched |= takeString(playerStore, payload, kApRegex);
    touched |= takeInt(playerStore, payload, kApTimeout);
    touched |= takeBoolean(playerStore, payload, kSkipIntro);
    touched |= takeStringSet(playerStore, payload, kAutoSkipTypes);
    touched |= takeBoolean(playerStore, payload, kAnimeSkip);
    touched |= takeString(playerStore, payload, kAnimeSkipClient);
    // Credential accepted on apply like Compose replaceFromSyncPayload.
    touched |= takeString(playerStore, payload, kIntroDbKey);
    touched |= takeBoolean(playerStore, payload, kIntroSubmit);
    touched |= takeBoolean(playerStore, payload, kNextEp);
    touched |= takeBoolean(playerStore, payload, kNextEpFallback);
    touched |= takeBoolean(playerStore, payload, kPreferBinge);
    touched |= takeBoolean(playerStore, payload, kReuseBinge);
    touched |= takeString(playerStore, payload, kThresholdMode);
    touched |= takeFloat(playerStore, payload, kThresholdPct);
    touched |= takeFloat(playerStore, payload, kThresholdMin);
    touched |= takeBoolean(playerStore, payload, kUseLibass);
    touched |= takeString(playerStore, payload, kLibassRender);
    touched |= takeBoolean(playerStore, payload, kNvidiaRtx);

    if (touched) {
        playerStore.persist();
        return true;
    }
    return false;
} // end applyRemotePayload


} // namespace nuvio::settings
