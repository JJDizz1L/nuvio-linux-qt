#include "nuvio/settings/AppSettings.h"

#include <algorithm>
#include <cstdlib>

#include "nuvio/settings/SyncPlayerSettings.h"
#include <nuvio/settings/PropertiesStore.h>

namespace nuvio::settings {

// ---------------------------------------------------------------------------
// P4 blob-parity storage contract (migrated 2026-08-27 from Qt-line-local
// keys). Every setting below now lives in the SAME store + profile-scoped key
// + value format the Compose line writes, so the two builds (and later the
// remote profile sync) read/write one truth:
//
//   player_settings.properties  (ProfileScopedKey => "<key>_1")
//     preferred_audio_language        String  ("default"|"device"|"original"|code)
//     preferred_subtitle_language     String  ("none"|"device"|"forced"|code)
//     subtitle_use_forced_subtitles   Boolean
//     stream_cache_size               String  ("MB_64"|"MB_256"|"MB_512"|"GB_1"|"GB_2")
//     decoder_priority                Int     (0 device-only | 1 prefer-device | 2 prefer-app)
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
        .value_or(true);
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
    const QString preSubs   = preferredSubtitleLanguage();
    const bool    preForced = useForcedSubtitles();
    const QString preDecoder = decoderMode();
    const int     preCacheMb = cacheMb();

    const bool touched =
        PlayerSettingsSync::applyRemotePayload(m_store->player, payload);
    if (!touched) return false;

    if (preferredAudioLanguage() != preAudio)
        emit preferredAudioLanguageChanged();
    if (preferredSubtitleLanguage() != preSubs)
        emit preferredSubtitleLanguageChanged();
    if (useForcedSubtitles() != preForced)
        emit useForcedSubtitlesChanged();
    if (decoderMode() != preDecoder) emit decoderModeChanged();
    if (cacheMb() != preCacheMb) emit cacheMbChanged();
    return true;
}

} // namespace nuvio::settings