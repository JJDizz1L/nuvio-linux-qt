#include "nuvio/tmdb/TmdbSettings.h"

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncPreferenceJson.h"

namespace nuvio::tmdb {

namespace {

constexpr auto kStoreFile = "tmdb_settings";

constexpr const char* kEnabled = "tmdb_enabled";
constexpr const char* kApiKey = "tmdb_api_key";
constexpr const char* kLanguage = "tmdb_language";
constexpr const char* kUseTrailers = "tmdb_use_trailers";
constexpr const char* kUseArtwork = "tmdb_use_artwork";
constexpr const char* kUseBasicInfo = "tmdb_use_basic_info";
constexpr const char* kUseDetails = "tmdb_use_details";
constexpr const char* kUseReleaseDates = "tmdb_use_release_dates";
constexpr const char* kUseCredits = "tmdb_use_credits";
constexpr const char* kUseProductions = "tmdb_use_productions";
constexpr const char* kUseNetworks = "tmdb_use_networks";
constexpr const char* kUseEpisodes = "tmdb_use_episodes";
constexpr const char* kUseSeasonPosters = "tmdb_use_season_posters";
constexpr const char* kUseMoreLikeThis = "tmdb_use_more_like_this";
constexpr const char* kUseCollections = "tmdb_use_collections";

[[nodiscard]] nuvio::settings::PropertiesStore openStore()
{
    return nuvio::settings::PropertiesStore(
        nuvio::settings::PropertiesStore::defaultPath(kStoreFile));
}

} // namespace

QString normalizeLanguage(const QString& value)
{
    QString out = value.trimmed();
    out.replace(u'_', u'-');
    return out;
}

TmdbSettings::TmdbSettings(QObject* parent)
    : QObject(parent),
      m_profileId(nuvio::settings::ActiveProfile::id())
{
}

QString TmdbSettings::scoped(const char* key) const
{
    return QString::fromLatin1(key) + u'_' + QString::number(m_profileId);
}

void TmdbSettings::load()
{
    m_loaded = true;
    auto store = openStore();
    const auto getBool = [&](const char* key,
                             bool fallback) -> bool {
        const std::string k = scoped(key).toStdString();
        if (!store.contains(k)) return fallback;
        return store.getBoolean(k).value_or(fallback);
    };
    const auto getString = [&](const char* key) -> QString {
        const auto v = store.getString(scoped(key).toStdString());
        return v ? QString::fromStdString(*v) : QString();
    };
    m_apiKey = getString(kApiKey).trimmed();
    // Enabled never survives without a key (fork loadFromDisk parity).
    m_enabled = getBool(kEnabled, false) && !m_apiKey.isEmpty();
    const QString storedLanguage = getString(kLanguage);
    m_language = storedLanguage.isNull() ? QStringLiteral("en")
                                         : normalizeLanguage(storedLanguage);
    m_useTrailers = getBool(kUseTrailers, true);
    m_useArtwork = getBool(kUseArtwork, true);
    m_useBasicInfo = getBool(kUseBasicInfo, true);
    m_useDetails = getBool(kUseDetails, true);
    m_useReleaseDates = getBool(kUseReleaseDates, false);
    m_useCredits = getBool(kUseCredits, true);
    m_useProductions = getBool(kUseProductions, true);
    m_useNetworks = getBool(kUseNetworks, true);
    m_useEpisodes = getBool(kUseEpisodes, true);
    m_useSeasonPosters = getBool(kUseSeasonPosters, true);
    m_useMoreLikeThis = getBool(kUseMoreLikeThis, true);
    m_useCollections = getBool(kUseCollections, true);
    publish();
}

void TmdbSettings::publish() { emit changed(); }

void TmdbSettings::setEnabled(bool value)
{
    if (!m_loaded) load();
    // Enabling without a key is a no-op (fork setEnabled parity).
    if (value && m_apiKey.isEmpty()) return;
    if (m_enabled == value) return;
    m_enabled = value;
    openStore().putBoolean(scoped(kEnabled).toStdString(), value);
    publish();
}

void TmdbSettings::setApiKey(const QString& value)
{
    if (!m_loaded) load();
    const QString normalized = value.trimmed();
    if (m_apiKey == normalized) return;
    m_apiKey = normalized;
    auto store = openStore();
    if (m_apiKey.isEmpty()) {
        // Clearing the key disables enrichment (fork parity).
        m_enabled = false;
        store.putBoolean(scoped(kEnabled).toStdString(), false);
    }
    store.putString(scoped(kApiKey).toStdString(),
                    m_apiKey.toStdString());
    publish();
}

void TmdbSettings::setLanguage(const QString& value)
{
    if (!m_loaded) load();
    const QString normalized = normalizeLanguage(value);
    if (m_language == normalized) return;
    m_language = normalized;
    openStore().putString(scoped(kLanguage).toStdString(),
                          m_language.toStdString());
    publish();
}

#define NUVIO_TMDB_BOOL(name, Name)                                \
    void TmdbSettings::set##Name(bool value)                      \
    {                                                             \
        if (!m_loaded) load();                                    \
        if (m_##name == value) return;                            \
        m_##name = value;                                         \
        openStore().putBoolean(scoped(k##Name).toStdString(),     \
                               value);                            \
        publish();                                                \
    }

NUVIO_TMDB_BOOL(useTrailers, UseTrailers)
NUVIO_TMDB_BOOL(useArtwork, UseArtwork)
NUVIO_TMDB_BOOL(useBasicInfo, UseBasicInfo)
NUVIO_TMDB_BOOL(useDetails, UseDetails)
NUVIO_TMDB_BOOL(useReleaseDates, UseReleaseDates)
NUVIO_TMDB_BOOL(useCredits, UseCredits)
NUVIO_TMDB_BOOL(useProductions, UseProductions)
NUVIO_TMDB_BOOL(useNetworks, UseNetworks)
NUVIO_TMDB_BOOL(useEpisodes, UseEpisodes)
NUVIO_TMDB_BOOL(useSeasonPosters, UseSeasonPosters)
NUVIO_TMDB_BOOL(useMoreLikeThis, UseMoreLikeThis)
NUVIO_TMDB_BOOL(useCollections, UseCollections)

void TmdbSettings::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    m_profileId = profileId;
    load();
}

void TmdbSettings::reload() { load(); }

QJsonObject TmdbSettings::exportSyncPayload() const
{
    // Present-only envelopes (fork exportToSyncPayload parity): absent
    // keys stay absent on the wire. Const-cast-free: read the store
    // directly (the cached members may predate external writes).
    using nuvio::settings::SyncPreferenceJson;
    auto store = openStore();
    QJsonObject out;
    const auto takeBool = [&](const char* key) {
        const std::string k = scoped(key).toStdString();
        if (!store.contains(k)) return;
        out.insert(QString::fromLatin1(key),
                   SyncPreferenceJson::encodeBoolean(
                       store.getBoolean(k).value_or(false)));
    };
    const auto takeString = [&](const char* key) {
        const std::string k = scoped(key).toStdString();
        if (!store.contains(k)) return;
        const auto v = store.getString(k);
        out.insert(QString::fromLatin1(key),
                   SyncPreferenceJson::encodeString(
                       v ? QString::fromStdString(*v) : QString()));
    };
    takeBool(kEnabled);
    takeString(kApiKey);
    takeString(kLanguage);
    takeBool(kUseTrailers);
    takeBool(kUseArtwork);
    takeBool(kUseBasicInfo);
    takeBool(kUseDetails);
    takeBool(kUseReleaseDates);
    takeBool(kUseCredits);
    takeBool(kUseProductions);
    takeBool(kUseNetworks);
    takeBool(kUseEpisodes);
    takeBool(kUseSeasonPosters);
    takeBool(kUseMoreLikeThis);
    takeBool(kUseCollections);
    return out;
}

bool TmdbSettings::applySyncPayload(const QJsonObject& payload)
{
    using nuvio::settings::SyncPreferenceJson;
    if (!m_loaded) load();
    auto store = openStore();
    bool touched = false;
    const auto takeBool = [&](const char* key) {
        const auto v = SyncPreferenceJson::decodeBoolean(
            payload, QString::fromLatin1(key));
        if (!v) return;
        store.putBoolean(scoped(key).toStdString(), *v);
        touched = true;
    };
    const auto takeString = [&](const char* key) {
        const auto v = SyncPreferenceJson::decodeString(
            payload, QString::fromLatin1(key));
        if (!v) return;
        store.putString(scoped(key).toStdString(), v->toStdString());
        touched = true;
    };
    takeBool(kEnabled);
    takeString(kApiKey);
    takeString(kLanguage);
    takeBool(kUseTrailers);
    takeBool(kUseArtwork);
    takeBool(kUseBasicInfo);
    takeBool(kUseDetails);
    takeBool(kUseReleaseDates);
    takeBool(kUseCredits);
    takeBool(kUseProductions);
    takeBool(kUseNetworks);
    takeBool(kUseEpisodes);
    takeBool(kUseSeasonPosters);
    takeBool(kUseMoreLikeThis);
    takeBool(kUseCollections);
    if (touched) load();   // re-resolve the enabled&&key gate
    return touched;
}

} // namespace nuvio::tmdb
