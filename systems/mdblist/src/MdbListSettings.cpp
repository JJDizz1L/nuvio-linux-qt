#include "nuvio/mdblist/MdbListSettings.h"

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SyncPreferenceJson.h"

namespace nuvio::mdblist {

namespace {

constexpr auto kStoreFile = "mdblist_settings";

constexpr const char* kEnabled = "mdblist_enabled";
constexpr const char* kApiKey = "mdblist_api_key";
constexpr const char* kUseImdb = "mdblist_use_imdb";
constexpr const char* kUseTmdb = "mdblist_use_tmdb";
constexpr const char* kUseTomatoes = "mdblist_use_tomatoes";
constexpr const char* kUseMetacritic = "mdblist_use_metacritic";
constexpr const char* kUseTrakt = "mdblist_use_trakt";
constexpr const char* kUseLetterboxd = "mdblist_use_letterboxd";
constexpr const char* kUseAudience = "mdblist_use_audience";
constexpr const char* kUseMal = "mdblist_use_mal";

[[nodiscard]] nuvio::settings::PropertiesStore openStore()
{
    return nuvio::settings::PropertiesStore(
        nuvio::settings::PropertiesStore::defaultPath(kStoreFile));
}

} // namespace

MdbListSettings::MdbListSettings(QObject* parent)
    : QObject(parent),
      m_profileId(nuvio::settings::ActiveProfile::id())
{
}

QString MdbListSettings::scoped(const char* key) const
{
    return QString::fromLatin1(key) + u'_' + QString::number(m_profileId);
}

bool MdbListSettings::isProviderEnabled(const QString& providerId) const
{
    const QString id = providerId.trimmed().toLower();
    if (id == QLatin1String("imdb")) return m_useImdb;
    if (id == QLatin1String("tmdb")) return m_useTmdb;
    if (id == QLatin1String("tomatoes")) return m_useTomatoes;
    if (id == QLatin1String("metacritic")) return m_useMetacritic;
    if (id == QLatin1String("trakt")) return m_useTrakt;
    if (id == QLatin1String("letterboxd")) return m_useLetterboxd;
    if (id == QLatin1String("audience")) return m_useAudience;
    if (id == QLatin1String("mal")) return m_useMal;
    return false;   // unknown ids are never enabled (fork parity)
}

QStringList MdbListSettings::enabledProviders() const
{
    QStringList out;
    for (const QLatin1String id :
         {QLatin1String("imdb"), QLatin1String("tmdb"),
          QLatin1String("tomatoes"), QLatin1String("metacritic"),
          QLatin1String("trakt"), QLatin1String("letterboxd"),
          QLatin1String("audience"), QLatin1String("mal")}) {
        if (isProviderEnabled(id)) out.append(id);
    }
    return out;
}

void MdbListSettings::load()
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
    m_useImdb = getBool(kUseImdb, true);
    m_useTmdb = getBool(kUseTmdb, true);
    m_useTomatoes = getBool(kUseTomatoes, true);
    m_useMetacritic = getBool(kUseMetacritic, true);
    m_useTrakt = getBool(kUseTrakt, true);
    m_useLetterboxd = getBool(kUseLetterboxd, true);
    m_useAudience = getBool(kUseAudience, true);
    m_useMal = getBool(kUseMal, true);
    publish();
}

void MdbListSettings::publish() { emit changed(); }

void MdbListSettings::setEnabled(bool value)
{
    if (!m_loaded) load();
    // Enabling without a key is a no-op (fork setEnabled parity).
    if (value && m_apiKey.isEmpty()) return;
    if (m_enabled == value) return;
    m_enabled = value;
    openStore().putBoolean(scoped(kEnabled).toStdString(), value);
    publish();
}

void MdbListSettings::setApiKey(const QString& value)
{
    if (!m_loaded) load();
    const QString normalized = value.trimmed();
    if (m_apiKey == normalized) return;
    m_apiKey = normalized;
    auto store = openStore();
    if (m_apiKey.isEmpty()) {
        // Clearing the key disables ratings (fork parity).
        m_enabled = false;
        store.putBoolean(scoped(kEnabled).toStdString(), false);
    }
    store.putString(scoped(kApiKey).toStdString(),
                    m_apiKey.toStdString());
    publish();
}

void MdbListSettings::setProviderEnabled(const QString& providerId,
                                         bool value)
{
    if (!m_loaded) load();
    const QString id = providerId.trimmed().toLower();
    bool* slot = nullptr;
    const char* key = nullptr;
    if (id == QLatin1String("imdb")) {
        slot = &m_useImdb;
        key = kUseImdb;
    } else if (id == QLatin1String("tmdb")) {
        slot = &m_useTmdb;
        key = kUseTmdb;
    } else if (id == QLatin1String("tomatoes")) {
        slot = &m_useTomatoes;
        key = kUseTomatoes;
    } else if (id == QLatin1String("metacritic")) {
        slot = &m_useMetacritic;
        key = kUseMetacritic;
    } else if (id == QLatin1String("trakt")) {
        slot = &m_useTrakt;
        key = kUseTrakt;
    } else if (id == QLatin1String("letterboxd")) {
        slot = &m_useLetterboxd;
        key = kUseLetterboxd;
    } else if (id == QLatin1String("audience")) {
        slot = &m_useAudience;
        key = kUseAudience;
    } else if (id == QLatin1String("mal")) {
        slot = &m_useMal;
        key = kUseMal;
    } else {
        return;   // unknown ids ignored (fork parity)
    }
    if (*slot == value) return;
    *slot = value;
    openStore().putBoolean(scoped(key).toStdString(), value);
    publish();
}

void MdbListSettings::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    m_profileId = profileId;
    load();
}

void MdbListSettings::reload() { load(); }

QJsonObject MdbListSettings::exportSyncPayload() const
{
    // Present-only envelopes (fork exportToSyncPayload parity).
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
    takeBool(kUseImdb);
    takeBool(kUseTmdb);
    takeBool(kUseTomatoes);
    takeBool(kUseMetacritic);
    takeBool(kUseTrakt);
    takeBool(kUseLetterboxd);
    takeBool(kUseAudience);
    takeBool(kUseMal);
    return out;
}

bool MdbListSettings::applySyncPayload(const QJsonObject& payload)
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
    takeBool(kUseImdb);
    takeBool(kUseTmdb);
    takeBool(kUseTomatoes);
    takeBool(kUseMetacritic);
    takeBool(kUseTrakt);
    takeBool(kUseLetterboxd);
    takeBool(kUseAudience);
    takeBool(kUseMal);
    if (touched) load();   // re-resolve the enabled&&key gate
    return touched;
}

} // namespace nuvio::mdblist
