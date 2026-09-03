#include "nuvio/settings/AppSettings.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

#include "nuvio/settings/SyncPlayerSettings.h"
#include <nuvio/settings/PropertiesStore.h>

namespace nuvio::settings {

// ---------------------------------------------------------------------------
// P4 blob-parity storage contract (migrated 2026-08-27 from Qt-line-local
// keys). Every setting below now lives in the SAME store + profile-scoped key
// + value format the Compose line writes, so the two builds (and later the
// remote profile sync) read/write one truth:
//
//   player_settings.properties  (ProfileScopedKey => "<key>_1"; P1a covers
//     the full Linux-meaningful Compose sync-key set — ground truth
//     PlayerSettingsStorage.desktop.kt syncKeys minus android_*/ios_*):
//     show_loading_overlay Boolean(true) / show_parental_guide Boolean(true)
//     resize_mode String(Fit|Fill|Zoom|Stretch)
//     hold_to_speed_enabled Boolean(true) / hold_to_speed_value Float(2.0)
//     touch_gestures_enabled Boolean(true, storage-only on desktop Qt)
//     external_player_enabled/forward_subtitles/send_skip_segments Boolean(false)
//     external_player_id String(system = desktop default)
//     preferred_audio_language String(device) / secondary_* ("" = unset/null)
//     preferred_subtitle_language String(none) / secondary_* ("" = unset/null)
//     subtitle_text_color #FFFFFFFF / background #00000000 / outline #FF000000
//     subtitle_outline_enabled Boolean(true) / outline_width Int(2)
//     subtitle_bold Boolean(false) / font_size_sp Int(18, clamp 6..40)
//     subtitle_bottom_offset Int(20) / strip_sdh Boolean(false)
//     subtitle_use_forced_subtitles Boolean(false = Compose DEFAULT)
//     subtitle_show_only_preferred_languages Boolean(false)
//     addon_subtitle_startup_mode String(FAST_STARTUP, live-adopted default)
//     stream_reuse_last_link_enabled Boolean(false) / cache_hours Int(24)
//     decoder_priority Int(0|1|2) / stream_cache_size enum (slider MB mapping)
//     map_dv7_to_hevc Boolean(false) / tunneling_enabled Boolean(false)
//     stream_auto_play_mode String / source String(+ENABLED_PLUGINS_ONLY)
//     stream_auto_play_selected_addons/plugins StringSet(JSON, sorted)
//     stream_auto_play_regex String("") / timeout_seconds Int(3, snapped incl MAX)
//     skip_intro_enabled Boolean(true) / auto_skip_segment_types StringSet
//     animeskip_enabled Boolean(false) / animeskip_client_id String("")
//     introdb_api_key String("") CREDENTIAL: stored, never exported
//     intro_submit_enabled Boolean(false)
//     stream_auto_play_next_episode_enabled Boolean(false)
//     stream_auto_play_next_episode_fallback_enabled Boolean(true)
//     stream_auto_play_prefer_binge_group Boolean(true)
//     stream_auto_play_reuse_binge_group Boolean(true)
//     next_episode_threshold_mode String(PERCENTAGE|MINUTES_BEFORE_END)
//     next_episode_threshold_percent_v2 Float(99)
//     next_episode_threshold_minutes_before_end_v2 Float(2)
//     use_libass Boolean(false) / libass_render_type String(CUES)
//     nvidia_rtx_super_resolution_enabled Boolean(false, key parity only)
//   discord_settings.properties
//     discord_enabled                 Boolean
//   torrent_settings.properties
//     cache_size                      String  ("NONE"|"GB_2"|"GB_5"|"GB_10")
//
// theme_dark stays Qt-line-local by design: Compose's theme_settings carries
// a color-theme enum (CRIMSON..WHITE); no dark/light boolean exists there.
//
// Legacy Qt keys (settings/pref_audio_lang, pref_sub_lang, use_forced_subs,
// stream_cache_size int-MB, decoder_mode, discord_enabled; torrent_settings/
// cache_size unscoped) migrate one-time into the parity keys on first read
// and are left in place (harmless; never destroyed).
// ---------------------------------------------------------------------------

namespace {
constexpr int kProfileId = 1;   // Compose activeProfileIndex default

[[nodiscard]] std::string profileScoped(const char* key)
{
    return std::string(key) + "_" + std::to_string(kProfileId);
}

// stream_cache_size enum <-> MB int (Compose StreamCacheSize).
[[nodiscard]] QString cacheSizeEnumForMb(const int mb)
{
    if (mb <= 64)   return QStringLiteral("MB_64");
    if (mb <= 256)  return QStringLiteral("MB_256");
    if (mb <= 512)  return QStringLiteral("MB_512");
    if (mb <= 1024) return QStringLiteral("GB_1");
    return QStringLiteral("GB_2");
}
[[nodiscard]] int mbForCacheSizeEnum(const QString& e)
{
    if (e == QLatin1String("MB_64"))   return 64;
    if (e == QLatin1String("MB_512"))  return 512;
    if (e == QLatin1String("GB_1"))    return 1024;
    if (e == QLatin1String("GB_2"))    return 2048;
    return 256;    // MB_256 + unknown fallback (Compose default)
}

// decoder_priority <-> Qt mode words. Compose: 0 device-only, 1 prefer-device
// (default; the bridge maps 0/1 to the SAME app-controlled hwdec chain), 2
// prefer-app (= hwdec no). The explicit vaapi/nvdec options are a Qt-line
// superset; they persist as 0 and degrade to "auto" on read-back (behavior
// identical: the auto chain is vendor-gated vaapi/nvdec).
[[nodiscard]] int decoderPriorityForMode(const QString& mode)
{
    if (mode == QLatin1String("software")) return 2;
    if (mode == QLatin1String("vaapi") || mode == QLatin1String("nvdec"))
        return 0;
    return 1;   // auto
}
[[nodiscard]] QString modeForDecoderPriority(const int priority)
{
    return priority == 2 ? QStringLiteral("software") : QStringLiteral("auto");
}
} // namespace

class AppSettings::Store final {
public:
    PropertiesStore props{PropertiesStore::defaultPath("settings")};
    PropertiesStore player{PropertiesStore::defaultPath("player_settings")};
    PropertiesStore discord{PropertiesStore::defaultPath("discord_settings")};
};

class AppSettings::TorrentStore final {
public:
    PropertiesStore props{PropertiesStore::defaultPath("torrent_settings")};
};

AppSettings::AppSettings(QObject* parent)
    : QObject(parent),
      m_store(new Store),
      m_torrentStore(new TorrentStore)
{
}

bool AppSettings::darkTheme() const
{
    return m_store->props.getBoolean("theme_dark").value_or(true);
}

void AppSettings::setDarkTheme(const bool v)
{
    if (darkTheme() == v) return;
    m_store->props.putBoolean("theme_dark", v);
    emit darkThemeChanged();
}

// ---- player settings (Compose-parity store) ---------------------------------

QString AppSettings::decoderMode() const
{
    // Legacy first (one-time migration), then the parity key.
    if (const auto legacy = m_store->props.getString("decoder_mode")) {
        const QString mode = QString::fromStdString(*legacy);
        m_store->player.putInt(profileScoped("decoder_priority"),
                               decoderPriorityForMode(mode));
        m_store->props.remove("decoder_mode");
        return mode;
    }
    const auto raw = m_store->player.getInt(profileScoped("decoder_priority"));
    return modeForDecoderPriority(raw.value_or(1));
}

void AppSettings::setDecoderMode(const QString& v)
{
    if (decoderMode() == v) return;
    m_store->player.putInt(profileScoped("decoder_priority"),
                           decoderPriorityForMode(v));
    emit decoderModeChanged();
}

int AppSettings::cacheMb() const
{
    // Legacy int-MB migration, then the Compose enum.
    if (const auto legacy = m_store->props.getInt("stream_cache_size")) {
        m_store->player.putString(profileScoped("stream_cache_size"),
            cacheSizeEnumForMb(*legacy).toStdString());
        m_store->props.remove("stream_cache_size");
        return *legacy;
    }
    const auto raw =
        m_store->player.getString(profileScoped("stream_cache_size"));
    if (!raw) return 256;          // Compose default MB_256
    return mbForCacheSizeEnum(QString::fromStdString(*raw));
}

void AppSettings::setCacheMb(int v)
{
    v = std::clamp(v, 64, 2048);
    // Snap to the Compose enum set so the stored value is always a valid
    // StreamCacheSize name (the slider drags through intermediate values).
    v = mbForCacheSizeEnum(cacheSizeEnumForMb(v));
    if (cacheMb() == v) return;
    m_store->player.putString(profileScoped("stream_cache_size"),
                              cacheSizeEnumForMb(v).toStdString());
    emit cacheMbChanged();
}

// ---- torrent settings (Compose-parity store + profile-scoped key) -----------

namespace {
bool validTorrentCacheSize(const std::string& s)
{
    return s == "NONE" || s == "GB_2" || s == "GB_5" || s == "GB_10";
}
} // namespace

QString AppSettings::torrentCacheSize() const
{
    // Legacy unscoped key migrates into the profile-scoped parity key.
    if (const auto legacy = m_torrentStore->props.getString("cache_size")) {
        if (validTorrentCacheSize(*legacy)) {
            m_torrentStore->props.putString(profileScoped("cache_size"),
                                            *legacy);
        }
        m_torrentStore->props.remove("cache_size");
        return QString::fromStdString(*legacy);
    }
    const auto raw =
        m_torrentStore->props.getString(profileScoped("cache_size"));
    if (!raw || !validTorrentCacheSize(*raw))
        return QStringLiteral("GB_2");          // Compose default
    return QString::fromStdString(*raw);
}

void AppSettings::setTorrentCacheSize(const QString& v)
{
    const std::string bytes = v.toStdString();
    if (!validTorrentCacheSize(bytes)) return;
    if (torrentCacheSize() == v) return;
    m_torrentStore->props.putString(profileScoped("cache_size"), bytes);
    emit torrentCacheSizeChanged();
}

// ---- player track preferences (Compose-parity store) ------------------------

QString AppSettings::preferredAudioLanguage() const
{
    if (const auto legacy = m_store->props.getString("pref_audio_lang")) {
        m_store->player.putString(profileScoped("preferred_audio_language"),
                                  *legacy);
        m_store->props.remove("pref_audio_lang");
        return QString::fromStdString(*legacy);
    }
    return QString::fromStdString(
        m_store->player.getString(profileScoped("preferred_audio_language"))
            .value_or("device"));
}

void AppSettings::setPreferredAudioLanguage(const QString& v)
{
    if (preferredAudioLanguage() == v) return;
    m_store->player.putString(profileScoped("preferred_audio_language"),
                              v.toStdString());
    emit preferredAudioLanguageChanged();
}

QString AppSettings::preferredSubtitleLanguage() const
{
    if (const auto legacy = m_store->props.getString("pref_sub_lang")) {
        m_store->player.putString(profileScoped("preferred_subtitle_language"),
                                  *legacy);
        m_store->props.remove("pref_sub_lang");
        return QString::fromStdString(*legacy);
    }
    return QString::fromStdString(
        m_store->player.getString(profileScoped("preferred_subtitle_language"))
            .value_or("none"));
}

void AppSettings::setPreferredSubtitleLanguage(const QString& v)
{
    if (preferredSubtitleLanguage() == v) return;
    m_store->player.putString(profileScoped("preferred_subtitle_language"),
                              v.toStdString());
    emit preferredSubtitleLanguageChanged();
}

bool AppSettings::useForcedSubtitles() const
{
    if (const auto legacy = m_store->props.getBoolean("use_forced_subs")) {
        m_store->player.putBoolean(
            profileScoped("subtitle_use_forced_subtitles"), *legacy);
        m_store->props.remove("use_forced_subs");
        return *legacy;
    }
    return m_store->player
        .getBoolean(profileScoped("subtitle_use_forced_subtitles"))
        .value_or(false);   // Compose SubtitleStyleState.DEFAULT = false
}

void AppSettings::setUseForcedSubtitles(bool v)
{
    if (useForcedSubtitles() == v) return;
    m_store->player.putBoolean(profileScoped("subtitle_use_forced_subtitles"),
                               v);
    emit useForcedSubtitlesChanged();
}

// ---- discord (Compose-parity store) -----------------------------------------

bool AppSettings::discordEnabled() const
{
    if (const auto legacy = m_store->props.getBoolean("discord_enabled")) {
        m_store->discord.putBoolean(profileScoped("discord_enabled"), *legacy);
        m_store->props.remove("discord_enabled");
        return *legacy;
    }
    return m_store->discord.getBoolean(profileScoped("discord_enabled"))
        .value_or(false);
}

void AppSettings::setDiscordEnabled(bool v)
{
    if (discordEnabled() == v) return;
    m_store->discord.putBoolean(profileScoped("discord_enabled"), v);
    emit discordEnabledChanged();
}

// ---- subtitle appearance (Compose-parity keys, mpv-applied) -----------------

bool AppSettings::subtitleOutlineEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("subtitle_outline_enabled")).value_or(true);
}

void AppSettings::setSubtitleOutlineEnabled(bool v)
{
    if (subtitleOutlineEnabled() == v) return;
    m_store->player.putBoolean(profileScoped("subtitle_outline_enabled"), v);
    emit subtitleStyleChanged();
}

int AppSettings::subtitleOutlineWidth() const
{
    return m_store->player.getInt(
        profileScoped("subtitle_outline_width")).value_or(2);
}

void AppSettings::setSubtitleOutlineWidth(int v)
{
    v = std::clamp(v, 0, 6);
    if (subtitleOutlineWidth() == v) return;
    m_store->player.putInt(profileScoped("subtitle_outline_width"), v);
    emit subtitleStyleChanged();
}

bool AppSettings::subtitleBold() const
{
    return m_store->player.getBoolean(
        profileScoped("subtitle_bold")).value_or(false);
}

void AppSettings::setSubtitleBold(bool v)
{
    if (subtitleBold() == v) return;
    m_store->player.putBoolean(profileScoped("subtitle_bold"), v);
    emit subtitleStyleChanged();
}

int AppSettings::subtitleBottomOffset() const
{
    return m_store->player.getInt(
        profileScoped("subtitle_bottom_offset")).value_or(20);
}

void AppSettings::setSubtitleBottomOffset(int v)
{
    v = std::clamp(v, 0, 200);
    if (subtitleBottomOffset() == v) return;
    m_store->player.putInt(profileScoped("subtitle_bottom_offset"), v);
    emit subtitleStyleChanged();
}

// ---- poster hover preview (Qt-line-local; defaults = Compose behavior) ------

bool AppSettings::hoverPreviewEnabled() const
{
    return m_store->props.getBoolean("hover_preview_enabled").value_or(true);
}

void AppSettings::setHoverPreviewEnabled(bool v)
{
    if (hoverPreviewEnabled() == v) return;
    m_store->props.putBoolean("hover_preview_enabled", v);
    emit hoverPreviewChanged();
}

int AppSettings::hoverPreviewDelayMs() const
{
    return m_store->props.getInt("hover_preview_delay_ms").value_or(2000);
}

void AppSettings::setHoverPreviewDelayMs(int v)
{
    v = std::clamp(v, 250, 10000);
    if (hoverPreviewDelayMs() == v) return;
    m_store->props.putInt("hover_preview_delay_ms", v);
    emit hoverPreviewChanged();
}

// ---- stream autoplay (Compose StreamAutoPlayMode/Source parity) -------------

namespace {
const char* const kAutoPlayModes[] = {"MANUAL", "FIRST_STREAM",
                                      "REGEX_MATCH"};
const char* const kAutoPlaySources[] = {"ALL_SOURCES",
                                        "INSTALLED_ADDONS_ONLY",
                                        "ENABLED_PLUGINS_ONLY"};
// Compose STREAM_AUTO_PLAY_TIMEOUT_VALUES + Int.MAX_VALUE ("no timeout").
// Ties break to the lower value; negatives snap to 0 (verbatim port of
// snapToAllowedTimeout; subtraction order avoids INT_MIN overflow).
const int kAutoPlayTimeouts[] = {0, 1, 2, 3, 4, 5,
                                 6, 7, 8, 9, 10, 15, 20, 25, 30,
                                 std::numeric_limits<int>::max()};

[[nodiscard]] bool oneOf(const QString& v, const char* const* list, int n)
{
    for (int i = 0; i < n; ++i)
        if (v == QLatin1String(list[i])) return true;
    return false;
}

[[nodiscard]] int snapTimeout(int v)
{
    if (v <= 0) return 0;
    int best = kAutoPlayTimeouts[0];
    long long bestDist = std::numeric_limits<long long>::max();
    for (int allowed : kAutoPlayTimeouts) {
        const long long a = static_cast<long long>(allowed);
        const long long b = static_cast<long long>(v);
        const long long d = a > b ? a - b : b - a;
        if (d < bestDist) {
            best = allowed;
            bestDist = d;
        }
    }
    return best;
}
} // namespace

QString AppSettings::streamAutoPlayMode() const
{
    const auto raw = m_store->player.getString(
        profileScoped("stream_auto_play_mode"));
    if (!raw) return QStringLiteral("MANUAL");
    const QString v = QString::fromStdString(*raw);
    return oneOf(v, kAutoPlayModes, 3) ? v : QStringLiteral("MANUAL");
}

void AppSettings::setStreamAutoPlayMode(const QString& v)
{
    if (!oneOf(v, kAutoPlayModes, 3) || streamAutoPlayMode() == v) return;
    m_store->player.putString(profileScoped("stream_auto_play_mode"),
                              v.toStdString());
    emit streamAutoPlayChanged();
}

QString AppSettings::streamAutoPlaySource() const
{
    const auto raw = m_store->player.getString(
        profileScoped("stream_auto_play_source"));
    if (!raw) return QStringLiteral("ALL_SOURCES");
    const QString v = QString::fromStdString(*raw);
    return oneOf(v, kAutoPlaySources, 3) ? v : QStringLiteral("ALL_SOURCES");
}

void AppSettings::setStreamAutoPlaySource(const QString& v)
{
    if (!oneOf(v, kAutoPlaySources, 3) || streamAutoPlaySource() == v) return;
    m_store->player.putString(profileScoped("stream_auto_play_source"),
                              v.toStdString());
    emit streamAutoPlayChanged();
}

int AppSettings::streamAutoPlayTimeoutSeconds() const
{
    return m_store->player.getInt(
        profileScoped("stream_auto_play_timeout_seconds")).value_or(3);
}

void AppSettings::setStreamAutoPlayTimeoutSeconds(int v)
{
    const int snapped = snapTimeout(v);
    if (streamAutoPlayTimeoutSeconds() == snapped) return;
    m_store->player.putInt(profileScoped("stream_auto_play_timeout_seconds"),
                           snapped);
    emit streamAutoPlayChanged();
}

QString AppSettings::streamAutoPlayRegex() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("stream_auto_play_regex")).value_or(""));
}

void AppSettings::setStreamAutoPlayRegex(const QString& v)
{
    if (streamAutoPlayRegex() == v) return;
    m_store->player.putString(profileScoped("stream_auto_play_regex"),
                              v.toStdString());
    emit streamAutoPlayChanged();
}

int AppSettings::subtitleFontSize() const
{
    const auto raw = m_store->player.getInt(
        profileScoped("subtitle_font_size_sp"));
    if (!raw) return 18;                                  // Compose default
    return std::clamp(*raw, 6, 40);                       // desktop range
}

void AppSettings::setSubtitleFontSize(int v)
{
    v = std::clamp(v, 6, 40);
    if (subtitleFontSize() == v) return;
    m_store->player.putInt(profileScoped("subtitle_font_size_sp"), v);
    emit subtitleStyleChanged();
}

QString AppSettings::subtitleTextColor() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("subtitle_text_color")).value_or("#FFFFFFFF"));
}

void AppSettings::setSubtitleTextColor(const QString& v)
{
    if (subtitleTextColor() == v) return;
    m_store->player.putString(profileScoped("subtitle_text_color"),
                              v.toStdString());
    emit subtitleStyleChanged();
}

// ---- P1a: full Linux-meaningful player-settings coverage -------------------
// Every getter/setter below follows the established parity pattern: read the
// profile-scoped Compose key, fall back to the Compose UiState/DEFAULT value
// when absent, validate enum words on read AND write (unknown -> default, no
// write). All emit playerOptionsChanged except the subtitle-family (shares
// subtitleStyleChanged for live mpv apply) and the autoplay sets (share
// streamAutoPlayChanged).

namespace {
// secondary languages: "" == unset (Compose null removes the key)
[[nodiscard]] QString secondaryOrEmpty(PropertiesStore& store,
                                      const std::string& scopedKey)
{
    const auto raw = store.getString(scopedKey);
    if (!raw || raw->empty()) return QString();
    return QString::fromStdString(*raw);
}

void setSecondary(PropertiesStore& store, const std::string& scopedKey,
                  const QString& v)
{
    if (v.isEmpty()) store.remove(scopedKey);
    else store.putString(scopedKey, v.toStdString());
}

[[nodiscard]] QStringList toQStringList(
    const std::optional<std::vector<std::string>>& v)
{
    QStringList out;
    if (!v) return out;
    for (const auto& s : *v) out.append(QString::fromStdString(s));
    return out;
}

[[nodiscard]] std::vector<std::string> sortedBytes(const QStringList& v)
{
    QStringList s = v;
    s.removeDuplicates();
    s.removeAll(QString());
    std::sort(s.begin(), s.end());
    std::vector<std::string> out;
    for (const auto& q : s) out.push_back(q.toStdString());
    return out;
}
} // namespace

QString AppSettings::secondaryPreferredAudioLanguage() const
{
    return secondaryOrEmpty(m_store->player,
                            profileScoped("secondary_preferred_audio_language"));
}

void AppSettings::setSecondaryPreferredAudioLanguage(const QString& v)
{
    if (secondaryPreferredAudioLanguage() == v) return;
    setSecondary(m_store->player,
                 profileScoped("secondary_preferred_audio_language"), v);
    emit preferredAudioLanguageChanged();
}

QString AppSettings::secondaryPreferredSubtitleLanguage() const
{
    return secondaryOrEmpty(
        m_store->player, profileScoped("secondary_preferred_subtitle_language"));
}

void AppSettings::setSecondaryPreferredSubtitleLanguage(const QString& v)
{
    if (secondaryPreferredSubtitleLanguage() == v) return;
    setSecondary(m_store->player,
                 profileScoped("secondary_preferred_subtitle_language"), v);
    emit preferredSubtitleLanguageChanged();
}

QString AppSettings::subtitleBackgroundColor() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("subtitle_background_color")).value_or("#00000000"));
}

void AppSettings::setSubtitleBackgroundColor(const QString& v)
{
    if (subtitleBackgroundColor() == v) return;
    m_store->player.putString(profileScoped("subtitle_background_color"),
                              v.toStdString());
    emit subtitleStyleChanged();
}

QString AppSettings::subtitleOutlineColor() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("subtitle_outline_color")).value_or("#FF000000"));
}

void AppSettings::setSubtitleOutlineColor(const QString& v)
{
    if (subtitleOutlineColor() == v) return;
    m_store->player.putString(profileScoped("subtitle_outline_color"),
                              v.toStdString());
    emit subtitleStyleChanged();
}

bool AppSettings::subtitleStripSdh() const
{
    return m_store->player.getBoolean(
        profileScoped("subtitle_strip_sdh")).value_or(false);
}

void AppSettings::setSubtitleStripSdh(bool v)
{
    if (subtitleStripSdh() == v) return;
    m_store->player.putBoolean(profileScoped("subtitle_strip_sdh"), v);
    emit subtitleStyleChanged();
}

bool AppSettings::subtitleShowOnlyPreferredLanguages() const
{
    return m_store->player.getBoolean(
        profileScoped("subtitle_show_only_preferred_languages"))
        .value_or(false);
}

void AppSettings::setSubtitleShowOnlyPreferredLanguages(bool v)
{
    if (subtitleShowOnlyPreferredLanguages() == v) return;
    m_store->player.putBoolean(
        profileScoped("subtitle_show_only_preferred_languages"), v);
    emit subtitleStyleChanged();
}

QString AppSettings::addonSubtitleStartupMode() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("addon_subtitle_startup_mode")).value_or("FAST_STARTUP"));
}

void AppSettings::setAddonSubtitleStartupMode(const QString& v)
{
    if (addonSubtitleStartupMode() == v) return;
    m_store->player.putString(profileScoped("addon_subtitle_startup_mode"),
                              v.toStdString());
    emit subtitleStyleChanged();
}

QStringList AppSettings::streamAutoPlaySelectedAddons() const
{
    return toQStringList(m_store->player.getStringSet(
        profileScoped("stream_auto_play_selected_addons")));
}

void AppSettings::setStreamAutoPlaySelectedAddons(const QStringList& v)
{
    if (streamAutoPlaySelectedAddons() == v) return;
    m_store->player.putStringSet(
        profileScoped("stream_auto_play_selected_addons"), sortedBytes(v));
    emit streamAutoPlayChanged();
}

QStringList AppSettings::streamAutoPlaySelectedPlugins() const
{
    return toQStringList(m_store->player.getStringSet(
        profileScoped("stream_auto_play_selected_plugins")));
}

void AppSettings::setStreamAutoPlaySelectedPlugins(const QStringList& v)
{
    if (streamAutoPlaySelectedPlugins() == v) return;
    m_store->player.putStringSet(
        profileScoped("stream_auto_play_selected_plugins"), sortedBytes(v));
    emit streamAutoPlayChanged();
}

bool AppSettings::showLoadingOverlay() const
{
    return m_store->player.getBoolean(
        profileScoped("show_loading_overlay")).value_or(true);
}

void AppSettings::setShowLoadingOverlay(bool v)
{
    if (showLoadingOverlay() == v) return;
    m_store->player.putBoolean(profileScoped("show_loading_overlay"), v);
    emit playerOptionsChanged();
}

bool AppSettings::showParentalGuide() const
{
    return m_store->player.getBoolean(
        profileScoped("show_parental_guide")).value_or(true);
}

void AppSettings::setShowParentalGuide(bool v)
{
    if (showParentalGuide() == v) return;
    m_store->player.putBoolean(profileScoped("show_parental_guide"), v);
    emit playerOptionsChanged();
}

QString AppSettings::resizeMode() const
{
    static const char* const kModes[] = {"Fit", "Fill", "Zoom", "Stretch"};
    const auto raw =
        m_store->player.getString(profileScoped("resize_mode"));
    if (!raw) return QStringLiteral("Fit");
    const QString v = QString::fromStdString(*raw);
    return oneOf(v, kModes, 4) ? v : QStringLiteral("Fit");
}

void AppSettings::setResizeMode(const QString& v)
{
    static const char* const kModes[] = {"Fit", "Fill", "Zoom", "Stretch"};
    if (!oneOf(v, kModes, 4) || resizeMode() == v) return;
    m_store->player.putString(profileScoped("resize_mode"), v.toStdString());
    emit playerOptionsChanged();
}

bool AppSettings::holdToSpeedEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("hold_to_speed_enabled")).value_or(true);
}

void AppSettings::setHoldToSpeedEnabled(bool v)
{
    if (holdToSpeedEnabled() == v) return;
    m_store->player.putBoolean(profileScoped("hold_to_speed_enabled"), v);
    emit playerOptionsChanged();
}

float AppSettings::holdToSpeedValue() const
{
    return m_store->player.getFloat(
        profileScoped("hold_to_speed_value")).value_or(2.0f);
}

void AppSettings::setHoldToSpeedValue(float v)
{
    v = std::clamp(v, 0.5f, 4.0f);
    if (holdToSpeedValue() == v) return;
    m_store->player.putFloat(profileScoped("hold_to_speed_value"), v);
    emit playerOptionsChanged();
}

bool AppSettings::touchGesturesEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("touch_gestures_enabled")).value_or(true);
}

void AppSettings::setTouchGesturesEnabled(bool v)
{
    if (touchGesturesEnabled() == v) return;
    m_store->player.putBoolean(profileScoped("touch_gestures_enabled"), v);
    emit playerOptionsChanged();
}

bool AppSettings::mapDv7ToHevc() const
{
    return m_store->player.getBoolean(
        profileScoped("map_dv7_to_hevc")).value_or(false);
}

void AppSettings::setMapDv7ToHevc(bool v)
{
    if (mapDv7ToHevc() == v) return;
    m_store->player.putBoolean(profileScoped("map_dv7_to_hevc"), v);
    emit playerOptionsChanged();
}

bool AppSettings::tunnelingEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("tunneling_enabled")).value_or(false);
}

void AppSettings::setTunnelingEnabled(bool v)
{
    if (tunnelingEnabled() == v) return;
    m_store->player.putBoolean(profileScoped("tunneling_enabled"), v);
    emit playerOptionsChanged();
}

bool AppSettings::useLibass() const
{
    return m_store->player.getBoolean(
        profileScoped("use_libass")).value_or(false);
}

void AppSettings::setUseLibass(bool v)
{
    if (useLibass() == v) return;
    m_store->player.putBoolean(profileScoped("use_libass"), v);
    emit playerOptionsChanged();
}

QString AppSettings::libassRenderType() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("libass_render_type")).value_or("CUES"));
}

void AppSettings::setLibassRenderType(const QString& v)
{
    if (libassRenderType() == v) return;
    m_store->player.putString(profileScoped("libass_render_type"),
                              v.toStdString());
    emit playerOptionsChanged();
}

bool AppSettings::nvidiaRtxSuperResolutionEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("nvidia_rtx_super_resolution_enabled")).value_or(false);
}

void AppSettings::setNvidiaRtxSuperResolutionEnabled(bool v)
{
    if (nvidiaRtxSuperResolutionEnabled() == v) return;
    m_store->player.putBoolean(
        profileScoped("nvidia_rtx_super_resolution_enabled"), v);
    emit playerOptionsChanged();
}

bool AppSettings::externalPlayerEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("external_player_enabled")).value_or(false);
}

void AppSettings::setExternalPlayerEnabled(bool v)
{
    if (externalPlayerEnabled() == v) return;
    m_store->player.putBoolean(profileScoped("external_player_enabled"), v);
    emit playerOptionsChanged();
}

bool AppSettings::externalPlayerForwardSubtitles() const
{
    return m_store->player.getBoolean(
        profileScoped("external_player_forward_subtitles")).value_or(false);
}

void AppSettings::setExternalPlayerForwardSubtitles(bool v)
{
    if (externalPlayerForwardSubtitles() == v) return;
    m_store->player.putBoolean(
        profileScoped("external_player_forward_subtitles"), v);
    emit playerOptionsChanged();
}

bool AppSettings::externalPlayerSendSkipSegments() const
{
    return m_store->player.getBoolean(
        profileScoped("external_player_send_skip_segments")).value_or(false);
}

void AppSettings::setExternalPlayerSendSkipSegments(bool v)
{
    if (externalPlayerSendSkipSegments() == v) return;
    m_store->player.putBoolean(
        profileScoped("external_player_send_skip_segments"), v);
    emit playerOptionsChanged();
}

QString AppSettings::externalPlayerId() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("external_player_id")).value_or("system"));
}

void AppSettings::setExternalPlayerId(const QString& v)
{
    if (externalPlayerId() == v) return;
    if (v.isEmpty()) m_store->player.remove(profileScoped("external_player_id"));
    else
        m_store->player.putString(profileScoped("external_player_id"),
                                  v.toStdString());
    emit playerOptionsChanged();
}

bool AppSettings::streamReuseLastLinkEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("stream_reuse_last_link_enabled")).value_or(false);
}

void AppSettings::setStreamReuseLastLinkEnabled(bool v)
{
    if (streamReuseLastLinkEnabled() == v) return;
    m_store->player.putBoolean(
        profileScoped("stream_reuse_last_link_enabled"), v);
    emit playerOptionsChanged();
}

int AppSettings::streamReuseLastLinkCacheHours() const
{
    return m_store->player.getInt(
        profileScoped("stream_reuse_last_link_cache_hours")).value_or(24);
}

void AppSettings::setStreamReuseLastLinkCacheHours(int v)
{
    v = std::max(v, 0);
    if (streamReuseLastLinkCacheHours() == v) return;
    m_store->player.putInt(
        profileScoped("stream_reuse_last_link_cache_hours"), v);
    emit playerOptionsChanged();
}

bool AppSettings::skipIntroEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("skip_intro_enabled")).value_or(true);
}

void AppSettings::setSkipIntroEnabled(bool v)
{
    if (skipIntroEnabled() == v) return;
    m_store->player.putBoolean(profileScoped("skip_intro_enabled"), v);
    emit playerOptionsChanged();
}

QStringList AppSettings::autoSkipSegmentTypes() const
{
    return toQStringList(m_store->player.getStringSet(
        profileScoped("auto_skip_segment_types")));
}

void AppSettings::setAutoSkipSegmentTypes(const QStringList& v)
{
    if (autoSkipSegmentTypes() == v) return;
    m_store->player.putStringSet(profileScoped("auto_skip_segment_types"),
                                 sortedBytes(v));
    emit playerOptionsChanged();
}

bool AppSettings::animeSkipEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("animeskip_enabled")).value_or(false);
}

void AppSettings::setAnimeSkipEnabled(bool v)
{
    if (animeSkipEnabled() == v) return;
    m_store->player.putBoolean(profileScoped("animeskip_enabled"), v);
    emit playerOptionsChanged();
}

QString AppSettings::animeSkipClientId() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("animeskip_client_id")).value_or(""));
}

void AppSettings::setAnimeSkipClientId(const QString& v)
{
    if (animeSkipClientId() == v) return;
    m_store->player.putString(profileScoped("animeskip_client_id"),
                              v.toStdString());
    emit playerOptionsChanged();
}

QString AppSettings::introDbApiKey() const
{
    return QString::fromStdString(m_store->player.getString(
        profileScoped("introdb_api_key")).value_or(""));
}

void AppSettings::setIntroDbApiKey(const QString& v)
{
    if (introDbApiKey() == v) return;
    m_store->player.putString(profileScoped("introdb_api_key"),
                              v.toStdString());
    emit playerOptionsChanged();
}

bool AppSettings::introSubmitEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("intro_submit_enabled")).value_or(false);
}

void AppSettings::setIntroSubmitEnabled(bool v)
{
    if (introSubmitEnabled() == v) return;
    m_store->player.putBoolean(profileScoped("intro_submit_enabled"), v);
    emit playerOptionsChanged();
}

bool AppSettings::streamAutoPlayNextEpisodeEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("stream_auto_play_next_episode_enabled"))
        .value_or(false);
}

void AppSettings::setStreamAutoPlayNextEpisodeEnabled(bool v)
{
    if (streamAutoPlayNextEpisodeEnabled() == v) return;
    m_store->player.putBoolean(
        profileScoped("stream_auto_play_next_episode_enabled"), v);
    emit playerOptionsChanged();
}

bool AppSettings::streamAutoPlayNextEpisodeFallbackEnabled() const
{
    return m_store->player.getBoolean(
        profileScoped("stream_auto_play_next_episode_fallback_enabled"))
        .value_or(true);
}

void AppSettings::setStreamAutoPlayNextEpisodeFallbackEnabled(bool v)
{
    if (streamAutoPlayNextEpisodeFallbackEnabled() == v) return;
    m_store->player.putBoolean(
        profileScoped("stream_auto_play_next_episode_fallback_enabled"), v);
    emit playerOptionsChanged();
}

bool AppSettings::streamAutoPlayPreferBingeGroup() const
{
    return m_store->player.getBoolean(
        profileScoped("stream_auto_play_prefer_binge_group")).value_or(true);
}

void AppSettings::setStreamAutoPlayPreferBingeGroup(bool v)
{
    if (streamAutoPlayPreferBingeGroup() == v) return;
    m_store->player.putBoolean(
        profileScoped("stream_auto_play_prefer_binge_group"), v);
    emit playerOptionsChanged();
}

bool AppSettings::streamAutoPlayReuseBingeGroup() const
{
    return m_store->player.getBoolean(
        profileScoped("stream_auto_play_reuse_binge_group")).value_or(true);
}

void AppSettings::setStreamAutoPlayReuseBingeGroup(bool v)
{
    if (streamAutoPlayReuseBingeGroup() == v) return;
    m_store->player.putBoolean(
        profileScoped("stream_auto_play_reuse_binge_group"), v);
    emit playerOptionsChanged();
}

QString AppSettings::nextEpisodeThresholdMode() const
{
    static const char* const kModes[] = {"PERCENTAGE",
                                         "MINUTES_BEFORE_END"};
    const auto raw = m_store->player.getString(
        profileScoped("next_episode_threshold_mode"));
    if (!raw) return QStringLiteral("PERCENTAGE");
    const QString v = QString::fromStdString(*raw);
    return oneOf(v, kModes, 2) ? v : QStringLiteral("PERCENTAGE");
}

void AppSettings::setNextEpisodeThresholdMode(const QString& v)
{
    static const char* const kModes[] = {"PERCENTAGE",
                                         "MINUTES_BEFORE_END"};
    if (!oneOf(v, kModes, 2) || nextEpisodeThresholdMode() == v) return;
    m_store->player.putString(profileScoped("next_episode_threshold_mode"),
                              v.toStdString());
    emit playerOptionsChanged();
}

float AppSettings::nextEpisodeThresholdPercent() const
{
    return m_store->player.getFloat(
        profileScoped("next_episode_threshold_percent_v2")).value_or(99.0f);
}

void AppSettings::setNextEpisodeThresholdPercent(float v)
{
    v = std::clamp(v, 0.0f, 100.0f);
    if (nextEpisodeThresholdPercent() == v) return;
    m_store->player.putFloat(
        profileScoped("next_episode_threshold_percent_v2"), v);
    emit playerOptionsChanged();
}

float AppSettings::nextEpisodeThresholdMinutesBeforeEnd() const
{
    return m_store->player.getFloat(
        profileScoped("next_episode_threshold_minutes_before_end_v2"))
        .value_or(2.0f);
}

void AppSettings::setNextEpisodeThresholdMinutesBeforeEnd(float v)
{
    v = std::max(v, 0.0f);
    if (nextEpisodeThresholdMinutesBeforeEnd() == v) return;
    m_store->player.putFloat(
        profileScoped("next_episode_threshold_minutes_before_end_v2"), v);
    emit playerOptionsChanged();
}

// ---- remote-profile-sync surface --------------------------------------------

QJsonObject AppSettings::exportPlayerSyncPayload()
{
    return PlayerSettingsSync::exportSyncPayload(m_store->player);
}

bool AppSettings::applyPlayerSyncPayload(const QJsonObject& payload)
{
    // Capture current values (getters read through our own cached store),
    // apply through the SAME instance, then emit only for values that
    // actually flipped so unaffected QML bindings never churn.
    const QString preAudio  = preferredAudioLanguage();
    const QString preSecAudio = secondaryPreferredAudioLanguage();
    const QString preSubs   = preferredSubtitleLanguage();
    const QString preSecSubs = secondaryPreferredSubtitleLanguage();
    const bool    preForced = useForcedSubtitles();
    const QString preDecoder = decoderMode();
    const int     preCacheMb = cacheMb();
    // Subtitle family (shares subtitleStyleChanged with the live applier).
    const bool    preStripSdh = subtitleStripSdh();
    const bool    preShowOnly = subtitleShowOnlyPreferredLanguages();
    const QString preAddonStartup = addonSubtitleStartupMode();
    const QString preSubBg = subtitleBackgroundColor();
    const QString preSubOutline = subtitleOutlineColor();
    const int     preSubSize = subtitleFontSize();
    const QString preSubColor = subtitleTextColor();
    const bool    preOutlineOn = subtitleOutlineEnabled();
    const int     preOutlineW = subtitleOutlineWidth();
    const bool    preSubBold = subtitleBold();
    const int     preSubOffset = subtitleBottomOffset();
    // Autoplay family (shares streamAutoPlayChanged).
    const QString preApMode = streamAutoPlayMode();
    const QString preApSource = streamAutoPlaySource();
    const int     preApTimeout = streamAutoPlayTimeoutSeconds();
    const QString preApRegex = streamAutoPlayRegex();
    const QStringList preApAddons = streamAutoPlaySelectedAddons();
    const QStringList preApPlugins = streamAutoPlaySelectedPlugins();
    // Behavior family (shares playerOptionsChanged): one signature over the
    // remaining getters is cheaper than 30 snapshots and equally exact for
    // change detection (values are only read, never parsed, here).
    const QString preBehavior =
        QStringList{
            showLoadingOverlay() ? QStringLiteral("1") : QStringLiteral("0"),
            showParentalGuide() ? QStringLiteral("1") : QStringLiteral("0"),
            resizeMode(),
            holdToSpeedEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            QString::number(holdToSpeedValue()),
            touchGesturesEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            mapDv7ToHevc() ? QStringLiteral("1") : QStringLiteral("0"),
            tunnelingEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            useLibass() ? QStringLiteral("1") : QStringLiteral("0"),
            libassRenderType(),
            nvidiaRtxSuperResolutionEnabled() ? QStringLiteral("1")
                                              : QStringLiteral("0"),
            externalPlayerEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            externalPlayerForwardSubtitles() ? QStringLiteral("1")
                                             : QStringLiteral("0"),
            externalPlayerSendSkipSegments() ? QStringLiteral("1")
                                             : QStringLiteral("0"),
            externalPlayerId(),
            streamReuseLastLinkEnabled() ? QStringLiteral("1")
                                         : QStringLiteral("0"),
            QString::number(streamReuseLastLinkCacheHours()),
            skipIntroEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            autoSkipSegmentTypes().join(QStringLiteral(";")),
            animeSkipEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            animeSkipClientId(), introDbApiKey(),
            introSubmitEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            streamAutoPlayNextEpisodeEnabled() ? QStringLiteral("1")
                                               : QStringLiteral("0"),
            streamAutoPlayNextEpisodeFallbackEnabled() ? QStringLiteral("1")
                                                       : QStringLiteral("0"),
            streamAutoPlayPreferBingeGroup() ? QStringLiteral("1")
                                             : QStringLiteral("0"),
            streamAutoPlayReuseBingeGroup() ? QStringLiteral("1")
                                            : QStringLiteral("0"),
            nextEpisodeThresholdMode(),
            QString::number(nextEpisodeThresholdPercent()),
            QString::number(nextEpisodeThresholdMinutesBeforeEnd()),
        }.join(QStringLiteral("|"));

    const bool touched =
        PlayerSettingsSync::applyRemotePayload(m_store->player, payload);
    if (!touched) return false;

    if (preferredAudioLanguage() != preAudio
        || secondaryPreferredAudioLanguage() != preSecAudio)
        emit preferredAudioLanguageChanged();
    if (preferredSubtitleLanguage() != preSubs
        || secondaryPreferredSubtitleLanguage() != preSecSubs)
        emit preferredSubtitleLanguageChanged();
    if (useForcedSubtitles() != preForced)
        emit useForcedSubtitlesChanged();
    if (decoderMode() != preDecoder) emit decoderModeChanged();
    if (cacheMb() != preCacheMb) emit cacheMbChanged();
    if (subtitleStripSdh() != preStripSdh
        || subtitleShowOnlyPreferredLanguages() != preShowOnly
        || addonSubtitleStartupMode() != preAddonStartup
        || subtitleBackgroundColor() != preSubBg
        || subtitleOutlineColor() != preSubOutline
        || subtitleFontSize() != preSubSize
        || subtitleTextColor() != preSubColor
        || subtitleOutlineEnabled() != preOutlineOn
        || subtitleOutlineWidth() != preOutlineW
        || subtitleBold() != preSubBold
        || subtitleBottomOffset() != preSubOffset)
        emit subtitleStyleChanged();
    if (streamAutoPlayMode() != preApMode
        || streamAutoPlaySource() != preApSource
        || streamAutoPlayTimeoutSeconds() != preApTimeout
        || streamAutoPlayRegex() != preApRegex
        || streamAutoPlaySelectedAddons() != preApAddons
        || streamAutoPlaySelectedPlugins() != preApPlugins)
        emit streamAutoPlayChanged();
    const QString postBehavior =
        QStringList{
            showLoadingOverlay() ? QStringLiteral("1") : QStringLiteral("0"),
            showParentalGuide() ? QStringLiteral("1") : QStringLiteral("0"),
            resizeMode(),
            holdToSpeedEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            QString::number(holdToSpeedValue()),
            touchGesturesEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            mapDv7ToHevc() ? QStringLiteral("1") : QStringLiteral("0"),
            tunnelingEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            useLibass() ? QStringLiteral("1") : QStringLiteral("0"),
            libassRenderType(),
            nvidiaRtxSuperResolutionEnabled() ? QStringLiteral("1")
                                              : QStringLiteral("0"),
            externalPlayerEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            externalPlayerForwardSubtitles() ? QStringLiteral("1")
                                             : QStringLiteral("0"),
            externalPlayerSendSkipSegments() ? QStringLiteral("1")
                                             : QStringLiteral("0"),
            externalPlayerId(),
            streamReuseLastLinkEnabled() ? QStringLiteral("1")
                                         : QStringLiteral("0"),
            QString::number(streamReuseLastLinkCacheHours()),
            skipIntroEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            autoSkipSegmentTypes().join(QStringLiteral(";")),
            animeSkipEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            animeSkipClientId(), introDbApiKey(),
            introSubmitEnabled() ? QStringLiteral("1") : QStringLiteral("0"),
            streamAutoPlayNextEpisodeEnabled() ? QStringLiteral("1")
                                               : QStringLiteral("0"),
            streamAutoPlayNextEpisodeFallbackEnabled() ? QStringLiteral("1")
                                                       : QStringLiteral("0"),
            streamAutoPlayPreferBingeGroup() ? QStringLiteral("1")
                                             : QStringLiteral("0"),
            streamAutoPlayReuseBingeGroup() ? QStringLiteral("1")
                                            : QStringLiteral("0"),
            nextEpisodeThresholdMode(),
            QString::number(nextEpisodeThresholdPercent()),
            QString::number(nextEpisodeThresholdMinutesBeforeEnd()),
        }.join(QStringLiteral("|"));
    if (postBehavior != preBehavior) emit playerOptionsChanged();
    return true;
}

} // namespace nuvio::settings