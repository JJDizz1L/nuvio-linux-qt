#include "nuvio/debrid/DebridSettings.h"

#include <algorithm>
#include <optional>

#include "nuvio/debrid/DebridTypes.h"
#include "nuvio/settings/SyncPreferenceJson.h"

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::debrid {

namespace {
// Compose DebridStreamFormatterDefaults verbatim (D2 implements the engine;
// the defaults must already round-trip today).
constexpr auto kDefaultNameTemplate =
    "{stream.resolution::exists[\"{stream.resolution} \"||\"\"]}"
    "{service.shortName::exists[\"{service.shortName}\"||\"Cloud\"]} Instant";
constexpr auto kDefaultDescriptionTemplate = "";

[[nodiscard]] std::string scoped(const char* key)
{
    return std::string(key) + "_" +
           std::to_string(nuvio::settings::ActiveProfile::id());
}

[[nodiscard]] nuvio::settings::PropertiesStore openStore()
{
    return nuvio::settings::PropertiesStore(
        nuvio::settings::PropertiesStore::defaultPath("debrid_settings"));
}
} // namespace

QString providerKeyId(const QString& providerId)
{
    const QString id = providerId.trimmed().toLower();
    if (id == QLatin1String("torbox") || id == QLatin1String("premiumize") ||
        id == QLatin1String("realdebrid"))
        return id;
    QString out;
    for (const QChar c : id)
        out.append(c.isLetterOrNumber() || c == u'_' ? c : u'_');
    return out;
}

QString providerApiKeyName(const QString& providerId)
{
    return QStringLiteral("debrid_") + providerKeyId(providerId) +
           QStringLiteral("_api_key");
}

DebridSettings::DebridSettings(QObject* parent) : QObject(parent) {}

std::optional<QString> DebridSettings::getString(const char* key) const
{
    auto store = openStore();
    const auto raw = store.getString(scoped(key));
    if (!raw) return std::nullopt;
    return QString::fromStdString(*raw);
}

void DebridSettings::putString(const char* key, const QString& value)
{
    auto store = openStore();
    store.putString(scoped(key), value.toStdString());
}

std::optional<bool> DebridSettings::getBool(const char* key) const
{
    auto store = openStore();
    return store.getBoolean(scoped(key));
}

void DebridSettings::putBool(const char* key, bool value)
{
    auto store = openStore();
    store.putBoolean(scoped(key), value);
}

std::optional<int> DebridSettings::getInt(const char* key) const
{
    auto store = openStore();
    const auto v = store.getInt(scoped(key));
    if (!v) return std::nullopt;
    return int(*v);
}

void DebridSettings::putInt(const char* key, int value)
{
    auto store = openStore();
    store.putInt(scoped(key), value);
}

bool DebridSettings::enabled() const
{
    return getBool("debrid_enabled").value_or(false);
}
void DebridSettings::setEnabled(bool v)
{
    if (enabled() == v) return;
    putBool("debrid_enabled", v);
    emit changed();
}

bool DebridSettings::cloudLibraryEnabled() const
{
    return getBool("debrid_cloud_library_enabled").value_or(true);
}
void DebridSettings::setCloudLibraryEnabled(bool v)
{
    if (cloudLibraryEnabled() == v) return;
    putBool("debrid_cloud_library_enabled", v);
    emit changed();
}

QString DebridSettings::preferredResolverProviderId() const
{
    return getString("debrid_preferred_resolver_provider_id").value_or("");
}
void DebridSettings::setPreferredResolverProviderId(const QString& v)
{
    if (preferredResolverProviderId() == v) return;
    putString("debrid_preferred_resolver_provider_id", v);
    emit changed();
}

int DebridSettings::instantPlaybackPreparationLimit() const
{
    return getInt("debrid_instant_playback_preparation_limit").value_or(0);
}
void DebridSettings::setInstantPlaybackPreparationLimit(int v)
{
    v = std::max(v, 0);
    if (instantPlaybackPreparationLimit() == v) return;
    putInt("debrid_instant_playback_preparation_limit", v);
    emit changed();
}

int DebridSettings::streamMaxResults() const
{
    return getInt("debrid_stream_max_results").value_or(0);
}
void DebridSettings::setStreamMaxResults(int v)
{
    v = std::max(v, 0);
    if (streamMaxResults() == v) return;
    putInt("debrid_stream_max_results", v);
    emit changed();
}

namespace {
QString validatedEnum(const std::optional<QString>& raw,
                      const char* const* allowed, int count,
                      const QString& fallback)
{
    if (!raw) return fallback;
    for (int i = 0; i < count; ++i) {
        if (*raw == QLatin1String(allowed[i])) return *raw;
    }
    return fallback;
}
} // namespace

QString DebridSettings::streamSortMode() const
{
    return validatedEnum(getString("debrid_stream_sort_mode"), kSortModes, 4,
                         QStringLiteral("DEFAULT"));
}
void DebridSettings::setStreamSortMode(const QString& v)
{
    QString validated = validatedEnum(v, kSortModes, 4, QString());
    if (validated.isEmpty() || streamSortMode() == validated) return;
    putString("debrid_stream_sort_mode", validated);
    emit changed();
}

QString DebridSettings::streamMinimumQuality() const
{
    return validatedEnum(getString("debrid_stream_minimum_quality"),
                         kMinQualities, 4, QStringLiteral("ANY"));
}
void DebridSettings::setStreamMinimumQuality(const QString& v)
{
    QString validated = validatedEnum(v, kMinQualities, 4, QString());
    if (validated.isEmpty() || streamMinimumQuality() == validated) return;
    putString("debrid_stream_minimum_quality", validated);
    emit changed();
}

QString DebridSettings::streamDolbyVisionFilter() const
{
    return validatedEnum(getString("debrid_stream_dolby_vision_filter"),
                         kFeatureFilters, 3, QStringLiteral("ANY"));
}
void DebridSettings::setStreamDolbyVisionFilter(const QString& v)
{
    QString validated = validatedEnum(v, kFeatureFilters, 3, QString());
    if (validated.isEmpty() || streamDolbyVisionFilter() == validated) return;
    putString("debrid_stream_dolby_vision_filter", validated);
    emit changed();
}

QString DebridSettings::streamHdrFilter() const
{
    return validatedEnum(getString("debrid_stream_hdr_filter"),
                         kFeatureFilters, 3, QStringLiteral("ANY"));
}
void DebridSettings::setStreamHdrFilter(const QString& v)
{
    QString validated = validatedEnum(v, kFeatureFilters, 3, QString());
    if (validated.isEmpty() || streamHdrFilter() == validated) return;
    putString("debrid_stream_hdr_filter", validated);
    emit changed();
}

QString DebridSettings::streamCodecFilter() const
{
    return validatedEnum(getString("debrid_stream_codec_filter"),
                         kCodecFilters, 4, QStringLiteral("ANY"));
}
void DebridSettings::setStreamCodecFilter(const QString& v)
{
    QString validated = validatedEnum(v, kCodecFilters, 4, QString());
    if (validated.isEmpty() || streamCodecFilter() == validated) return;
    putString("debrid_stream_codec_filter", validated);
    emit changed();
}

QString DebridSettings::streamPreferences() const
{
    return getString("debrid_stream_preferences").value_or("");
}
void DebridSettings::setStreamPreferences(const QString& v)
{
    if (streamPreferences() == v) return;
    putString("debrid_stream_preferences", v);
    emit changed();
}

QString DebridSettings::streamNameTemplate() const
{
    return getString("debrid_stream_name_template")
        .value_or(QString::fromLatin1(kDefaultNameTemplate));
}
void DebridSettings::setStreamNameTemplate(const QString& v)
{
    if (streamNameTemplate() == v) return;
    putString("debrid_stream_name_template", v);
    emit changed();
}

QString DebridSettings::streamDescriptionTemplate() const
{
    return getString("debrid_stream_description_template")
        .value_or(QString::fromLatin1(kDefaultDescriptionTemplate));
}
void DebridSettings::setStreamDescriptionTemplate(const QString& v)
{
    if (streamDescriptionTemplate() == v) return;
    putString("debrid_stream_description_template", v);
    emit changed();
}

QString DebridSettings::providerApiKey(const QString& providerId) const
{
    return getString(
               providerApiKeyName(providerId).toStdString().c_str())
        .value_or("");
}

void DebridSettings::setProviderApiKey(const QString& providerId,
                                       const QString& apiKey)
{
    if (providerApiKey(providerId) == apiKey) return;
    putString(providerApiKeyName(providerId).toStdString().c_str(), apiKey);
    emit changed();
}

QStringList DebridSettings::configuredProviderIds() const
{
    QStringList out;
    for (const DebridProvider& p : allProviders()) {
        if (!providerApiKey(p.id).trimmed().isEmpty()) out.append(p.id);
    }
    return out;
}

QString DebridSettings::pendingDeviceAuthorization(
    const QString& providerId) const
{
    const QString key = QStringLiteral(
                            "debrid_pending_device_authorization_") +
                        providerKeyId(providerId);
    return getString(key.toStdString().c_str()).value_or("");
}

void DebridSettings::setPendingDeviceAuthorization(const QString& providerId,
                                                   const QString& payload)
{
    const QString key = QStringLiteral(
                            "debrid_pending_device_authorization_") +
                        providerKeyId(providerId);
    putString(key.toStdString().c_str(), payload);
}

void DebridSettings::clearPendingDeviceAuthorization(
    const QString& providerId)
{
    auto store = openStore();
    const QString key = QStringLiteral(
                            "debrid_pending_device_authorization_") +
                        providerKeyId(providerId);
    store.remove((key + u'_' +
                  QString::number(nuvio::settings::ActiveProfile::id()))
                     .toStdString());
}

QJsonObject DebridSettings::exportSyncPayload()
{
    using nuvio::settings::SyncPreferenceJson;
    auto store = openStore();
    QJsonObject out;
    const auto putString = [&](const char* key) {
        if (const auto v = store.getString(scoped(key)))
            out.insert(QString::fromLatin1(key),
                       SyncPreferenceJson::encodeString(
                           QString::fromStdString(*v)));
    };
    const auto putBool = [&](const char* key) {
        if (const auto v = store.getBoolean(scoped(key)))
            out.insert(QString::fromLatin1(key),
                       SyncPreferenceJson::encodeBoolean(*v));
    };
    const auto putInt = [&](const char* key) {
        if (const auto v = store.getInt(scoped(key)))
            out.insert(QString::fromLatin1(key),
                       SyncPreferenceJson::encodeInt(*v));
    };
    putBool("debrid_enabled");
    putBool("debrid_cloud_library_enabled");
    putString("debrid_preferred_resolver_provider_id");
    for (const DebridProvider& p : allProviders()) {
        const std::string key =
            providerApiKeyName(p.id).toStdString() + "_" +
            std::to_string(nuvio::settings::ActiveProfile::id());
        if (const auto v = store.getString(key))
            out.insert(providerApiKeyName(p.id),
                       SyncPreferenceJson::encodeString(
                           QString::fromStdString(*v)));
    }
    putInt("debrid_instant_playback_preparation_limit");
    putInt("debrid_stream_max_results");
    putString("debrid_stream_sort_mode");
    putString("debrid_stream_minimum_quality");
    putString("debrid_stream_dolby_vision_filter");
    putString("debrid_stream_hdr_filter");
    putString("debrid_stream_codec_filter");
    putString("debrid_stream_preferences");
    putString("debrid_stream_name_template");
    putString("debrid_stream_description_template");
    return out;
}

bool DebridSettings::applySyncPayload(const QJsonObject& payload)
{
    using nuvio::settings::SyncPreferenceJson;
    auto store = openStore();
    bool touched = false;
    const auto takeString = [&](const char* key) {
        const auto v = SyncPreferenceJson::decodeString(
            payload, QString::fromLatin1(key));
        if (!v) return;
        store.putString(scoped(key), v->toStdString());
        touched = true;
    };
    const auto takeBool = [&](const char* key) {
        const auto v = SyncPreferenceJson::decodeBoolean(
            payload, QString::fromLatin1(key));
        if (!v) return;
        store.putBoolean(scoped(key), *v);
        touched = true;
    };
    const auto takeInt = [&](const char* key) {
        const auto v = SyncPreferenceJson::decodeInt(
            payload, QString::fromLatin1(key));
        if (!v) return;
        store.putInt(scoped(key), *v);
        touched = true;
    };
    takeBool("debrid_enabled");
    takeBool("debrid_cloud_library_enabled");
    takeString("debrid_preferred_resolver_provider_id");
    for (const DebridProvider& p : allProviders()) {
        const QString key = providerApiKeyName(p.id);
        const auto v =
            SyncPreferenceJson::decodeString(payload, key);
        if (!v) continue;
        store.putString(
            (key + u'_' +
             QString::number(nuvio::settings::ActiveProfile::id()))
                .toStdString(),
            v->toStdString());
        touched = true;
    }
    takeInt("debrid_instant_playback_preparation_limit");
    takeInt("debrid_stream_max_results");
    takeString("debrid_stream_sort_mode");
    takeString("debrid_stream_minimum_quality");
    takeString("debrid_stream_dolby_vision_filter");
    takeString("debrid_stream_hdr_filter");
    takeString("debrid_stream_codec_filter");
    takeString("debrid_stream_preferences");
    takeString("debrid_stream_name_template");
    takeString("debrid_stream_description_template");
    if (touched) emit changed();
    return touched;
}

} // namespace nuvio::debrid
