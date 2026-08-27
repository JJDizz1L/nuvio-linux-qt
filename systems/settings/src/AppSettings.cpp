#include "nuvio/settings/AppSettings.h"

#include <algorithm>

#include <nuvio/settings/PropertiesStore.h>

namespace nuvio::settings {

// Storage keys - shared contract with the Compose line (AGENTS.md rebranding
// & portability rules: these are profile data, not branding).
namespace keys {
constexpr auto kDarkTheme  = "theme_dark";
constexpr auto kDecoder    = "decoder_mode";
constexpr auto kCacheMb    = "stream_cache_size";   // exact Compose key; unit (MB) verified during parity pass
constexpr auto kTorrentCacheSize = "cache_size";    // exact Compose key in store "torrent_settings"
// Track prefs (Qt-line-local spellings; upstream blob parity = P4 task)
constexpr auto kPrefAudio     = "pref_audio_lang";
constexpr auto kPrefSub       = "pref_sub_lang";
constexpr auto kPrefSubtitle  = "pref_sub_lang";
constexpr auto kForcedSubs    = "use_forced_subs";
} // namespace

class AppSettings::Store final {
public:
    PropertiesStore props{PropertiesStore::defaultPath("settings")};
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
    return m_store->props.getBoolean(keys::kDarkTheme).value_or(true);
}

void AppSettings::setDarkTheme(const bool v)
{
    if (darkTheme() == v) return;
    m_store->props.putBoolean(keys::kDarkTheme, v);
    emit darkThemeChanged();
}

QString AppSettings::decoderMode() const
{
    return QString::fromStdString(
        m_store->props.getString(keys::kDecoder).value_or("auto"));
}

void AppSettings::setDecoderMode(const QString& v)
{
    if (decoderMode() == v) return;
    m_store->props.putString(keys::kDecoder, v.toStdString());
    emit decoderModeChanged();
}

int AppSettings::cacheMb() const
{
    const auto raw = m_store->props.getInt(keys::kCacheMb);
    if (!raw || *raw <= 0) return 256;          // Compose default cache value
    return *raw;
}

void AppSettings::setCacheMb(int v)
{
    v = std::clamp(v, 64, 2048);
    if (cacheMb() == v) return;
    m_store->props.putInt(keys::kCacheMb, v);
    emit cacheMbChanged();
}

// ---- torrent settings (separate "torrent_settings" store) -----------------

namespace {
bool validTorrentCacheSize(const std::string& s)
{
    return s == "NONE" || s == "GB_2" || s == "GB_5" || s == "GB_10";
}
} // namespace

QString AppSettings::torrentCacheSize() const
{
    const auto raw = m_torrentStore->props.getString(keys::kTorrentCacheSize);
    if (!raw || !validTorrentCacheSize(*raw))
        return QStringLiteral("GB_2");          // Compose default
    return QString::fromStdString(*raw);
}

void AppSettings::setTorrentCacheSize(const QString& v)
{
    const std::string bytes = v.toStdString();
    if (!validTorrentCacheSize(bytes)) return;
    if (torrentCacheSize() == v) return;
    m_torrentStore->props.putString(keys::kTorrentCacheSize, bytes);
    emit torrentCacheSizeChanged();
}

// ---- player track preferences ---------------------------------------------

QString AppSettings::preferredAudioLanguage() const
{
    return QString::fromStdString(
        m_store->props.getString(keys::kPrefAudio).value_or("device"));
}

void AppSettings::setPreferredAudioLanguage(const QString& v)
{
    if (preferredAudioLanguage() == v) return;
    m_store->props.putString(keys::kPrefAudio, v.toStdString());
    emit preferredAudioLanguageChanged();
}

QString AppSettings::preferredSubtitleLanguage() const
{
    return QString::fromStdString(
        m_store->props.getString(keys::kPrefSub).value_or("none"));
}

void AppSettings::setPreferredSubtitleLanguage(const QString& v)
{
    if (preferredSubtitleLanguage() == v) return;
    m_store->props.putString(keys::kPrefSub, v.toStdString());
    emit preferredSubtitleLanguageChanged();
}

bool AppSettings::useForcedSubtitles() const
{
    return m_store->props.getBoolean(keys::kForcedSubs).value_or(true);
}

void AppSettings::setUseForcedSubtitles(bool v)
{
    if (useForcedSubtitles() == v) return;
    m_store->props.putBoolean(keys::kForcedSubs, v);
    emit useForcedSubtitlesChanged();
}

} // namespace nuvio::settings