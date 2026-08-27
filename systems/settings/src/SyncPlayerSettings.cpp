#include "nuvio/settings/SyncPlayerSettings.h"

#include <QJsonObject>
#include <QString>

#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncPreferenceJson.h"

namespace nuvio::settings {

namespace {
constexpr int kProfileId = 1;   // matches AppSettings (activeProfileIndex)

// PropertiesStore keys are std::string_view-based (Java properties).
[[nodiscard]] std::string scoped(const char* key)
{
    return std::string(key) + "_" + std::to_string(kProfileId);
}

[[nodiscard]] QString scopedKey(const char* key)
{
    return QString::fromStdString(scoped(key));
}

// Compose PlayerSettingsStorage key names (unscoped forms).
constexpr auto kPreferredAudioLang = "preferred_audio_language";
constexpr auto kPreferredSubLang   = "preferred_subtitle_language";
constexpr auto kForcedSubtitles    = "subtitle_use_forced_subtitles";
constexpr auto kStreamCacheSize    = "stream_cache_size";
constexpr auto kDecoderPriority    = "decoder_priority";
} // namespace

QJsonObject PlayerSettingsSync::exportSyncPayload(
    nuvio::settings::PropertiesStore& playerStore)
{
    QJsonObject out;

    if (const auto v = playerStore.getString(scoped(kPreferredAudioLang)))
        out.insert(scopedKey(kPreferredAudioLang),
                   SyncPreferenceJson::encodeString(
                       QString::fromStdString(*v)));

    if (const auto v = playerStore.getString(scoped(kPreferredSubLang)))
        out.insert(scopedKey(kPreferredSubLang),
                   SyncPreferenceJson::encodeString(
                       QString::fromStdString(*v)));

    if (const auto v = playerStore.getBoolean(scoped(kForcedSubtitles)))
        out.insert(scopedKey(kForcedSubtitles),
                   SyncPreferenceJson::encodeBoolean(*v));

    if (const auto v = playerStore.getString(scoped(kStreamCacheSize)))
        out.insert(scopedKey(kStreamCacheSize),
                   SyncPreferenceJson::encodeString(
                       QString::fromStdString(*v)));

    if (const auto v = playerStore.getInt(scoped(kDecoderPriority)))
        out.insert(scopedKey(kDecoderPriority),
                   SyncPreferenceJson::encodeInt(*v));

    return out;
}

bool PlayerSettingsSync::applyRemotePayload(
    nuvio::settings::PropertiesStore& playerStore, const QJsonObject& payload)
{
    bool touched = false;

    // Per-key optional-let semantics: invalid/absent -> untouched.
    const auto audio =
        SyncPreferenceJson::decodeString(payload,
                                         scopedKey(kPreferredAudioLang));
    if (audio) {
        playerStore.putString(scoped(kPreferredAudioLang),
                              audio->toStdString());
        touched = true;
    }

    const auto subs =
        SyncPreferenceJson::decodeString(payload, scopedKey(kPreferredSubLang));
    if (subs) {
        playerStore.putString(scoped(kPreferredSubLang), subs->toStdString());
        touched = true;
    }

    const auto forced =
        SyncPreferenceJson::decodeBoolean(payload,
                                          scopedKey(kForcedSubtitles));
    if (forced) {
        playerStore.putBoolean(scoped(kForcedSubtitles), *forced);
        touched = true;
    }

    const auto cache =
        SyncPreferenceJson::decodeString(payload, scopedKey(kStreamCacheSize));
    if (cache) {
        playerStore.putString(scoped(kStreamCacheSize), cache->toStdString());
        touched = true;
    }

    const auto priority =
        SyncPreferenceJson::decodeInt(payload, scopedKey(kDecoderPriority));
    if (priority) {
        playerStore.putInt(scoped(kDecoderPriority), *priority);
        touched = true;
    }

    if (touched) {
        playerStore.persist();
        return true;
    }
    return false;
} // end applyRemotePayload


} // namespace nuvio::settings
