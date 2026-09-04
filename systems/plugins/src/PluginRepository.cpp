#include "nuvio/plugins/PluginRepository.h"

#include <algorithm>
#include <memory>

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkReply>
#include <QSysInfo>
#include <QUrl>

#include "nuvio/authsync/AuthConfig.h"
#include "nuvio/authsync/AuthService.h"
#include "nuvio/authsync/SyncRpcClient.h"
#include "nuvio/plugins/PluginCrypto.h"
#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/tmdb/TmdbService.h"

namespace nuvio::plugins {

namespace {

constexpr auto kStoreFile = "plugins";
constexpr qint64 kRefreshIntervalMs = 6LL * 60LL * 60LL * 1000LL;
constexpr int kMaxParallelExecutions = 4;   // throttle: fork is uncapped

[[nodiscard]] QString stateKey(int profileId)
{
    return QStringLiteral("plugins_state_") + QString::number(profileId);
}

[[nodiscard]] QString codeKey(int profileId, const QString& scraperId)
{
    // Fork scraperCodeKey parity (SHA256 hex of the scraper id).
    return QStringLiteral("scraper_code_") + QString::number(profileId) +
           u'_' +
           pluginBytesToHex(pluginDigest(QStringLiteral("SHA256"),
                                         scraperId.toUtf8()));
}

[[nodiscard]] QString settingsKey(const QString& scraperId)
{
    // Global (not profile-scoped), fork parity.
    return QStringLiteral("settings_") + scraperId;
}

[[nodiscard]] nuvio::settings::PropertiesStore openStore()
{
    return nuvio::settings::PropertiesStore(
        nuvio::settings::PropertiesStore::defaultPath(kStoreFile));
}

[[nodiscard]] QVariantMap repoVariant(const PluginRepositoryItem& r)
{
    return QVariantMap{
        {QStringLiteral("manifestUrl"), r.manifestUrl},
        {QStringLiteral("name"), r.name},
        {QStringLiteral("description"), r.description},
        {QStringLiteral("version"), r.version},
        {QStringLiteral("scraperCount"), r.scraperCount},
        {QStringLiteral("lastUpdated"), r.lastUpdated},
        {QStringLiteral("isRefreshing"), r.isRefreshing},
        {QStringLiteral("errorMessage"), r.errorMessage},
    };
}

[[nodiscard]] QVariantMap scraperVariant(const PluginScraper& s)
{
    return QVariantMap{
        {QStringLiteral("id"), s.id},
        {QStringLiteral("repositoryUrl"), s.repositoryUrl},
        {QStringLiteral("name"), s.name},
        {QStringLiteral("description"), s.description},
        {QStringLiteral("version"), s.version},
        {QStringLiteral("filename"), s.filename},
        {QStringLiteral("supportedTypes"), s.supportedTypes},
        {QStringLiteral("enabled"), s.enabled},
        {QStringLiteral("manifestEnabled"), s.manifestEnabled},
        {QStringLiteral("hasSettings"), s.hasSettings},
        {QStringLiteral("logo"), s.logo},
        {QStringLiteral("contentLanguage"), s.contentLanguage},
        {QStringLiteral("formats"), s.formats},
    };
}

[[nodiscard]] QStringList stringList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& e : v.toArray()) {
        const QString s = e.toString();
        if (!s.isEmpty()) out.append(s);
    }
    return out;
}

[[nodiscard]] QStringList toLowerList(const QStringList& in)
{
    QStringList out;
    for (const QString& s : in) out.append(s.toLower());
    return out;
}

} // namespace

QString encodeUnsafeHttpUrlCharacters(const QString& value)
{
    // Verbatim table (space " < > \ ^ ` { | } + C0 controls as UTF-8).
    static const char* kHex = "0123456789ABCDEF";
    QString out;
    out.reserve(value.size());
    for (const QChar c : value) {
        const ushort u = c.unicode();
        const char* mapped = nullptr;
        switch (u) {
        case u' ': mapped = "%20"; break;
        case u'"': mapped = "%22"; break;
        case u'<': mapped = "%3C"; break;
        case u'>': mapped = "%3E"; break;
        case u'\\': mapped = "%5C"; break;
        case u'^': mapped = "%5E"; break;
        case u'`': mapped = "%60"; break;
        case u'{': mapped = "%7B"; break;
        case u'|': mapped = "%7C"; break;
        case u'}': mapped = "%7D"; break;
        default: break;
        }
        if (mapped) {
            out += QString::fromLatin1(mapped);
        } else if (u <= 0x1f || u == 0x7f) {
            for (const char b : QString(c).toUtf8()) {
                const unsigned char ub = static_cast<unsigned char>(b);
                out += u'%';
                out += QChar::fromLatin1(kHex[(ub >> 4) & 0xf]);
                out += QChar::fromLatin1(kHex[ub & 0xf]);
            }
        } else {
            out.append(c);
        }
    }
    return out;
}

QString normalizeManifestUrl(const QString& rawUrl, QString* errorOut)
{
    const QString trimmed = rawUrl.trimmed();
    if (trimmed.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("Enter a plugin repository URL.");
        return {};
    }
    QString withScheme = trimmed;
    if (!withScheme.startsWith(QLatin1String("http://")) &&
        !withScheme.startsWith(QLatin1String("https://")))
        withScheme = QStringLiteral("https://") + withScheme;
    const QString withoutFragment = withScheme.section(u'#', 0, 0);
    const QString query = withoutFragment.section(u'?', 1);
    QString path = withoutFragment.section(u'?', 0, 0);
    while (path.endsWith(u'/')) path.chop(1);
    if (!path.endsWith(QLatin1String("/manifest.json")))
        path += QStringLiteral("/manifest.json");
    const QString manifestUrl =
        query.isEmpty() ? path : path + u'?' + query;
    if (!QUrl(manifestUrl).isValid() || manifestUrl.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("Enter a valid plugin URL.");
        return {};
    }
    return encodeUnsafeHttpUrlCharacters(manifestUrl);
}

QString pluginContentId(const QString& videoId, int season, int episode)
{
    const QString trimmed = videoId.trimmed();
    if (trimmed.isEmpty()) return videoId;
    QString id = trimmed;
    if (id.startsWith(QLatin1String("tmdb:")))
        id = id.mid(5);
    else if (id.startsWith(QLatin1String("tmdb/")))
        id = id.mid(5);
    if (season >= 0 && episode >= 0) {
        const QString suffix =
            u':' + QString::number(season) + u':' + QString::number(episode);
        if (id.endsWith(suffix)) id = id.left(id.size() - suffix.size());
    }
    const QString head = id.section(u'/', 0, 0);
    return head.isEmpty() ? trimmed : head;
}

bool isPluginRepositoryRefreshDue(qint64 lastUpdatedEpochMs, qint64 nowEpochMs)
{
    return lastUpdatedEpochMs <= 0 ||
           nowEpochMs - lastUpdatedEpochMs >= kRefreshIntervalMs;
}

QStringList currentPluginPlatformTags()
{
    QStringList tags{QStringLiteral("desktop"), QStringLiteral("qt")};
    const QString os = QSysInfo::productType().toLower();
    if (os.contains(QLatin1String("macos")) ||
        os.contains(QLatin1String("darwin")))
        tags.append(QStringLiteral("macos"));
    else if (os.contains(QLatin1String("windows")))
        tags.append(QStringLiteral("windows"));
    else
        tags.append(QStringLiteral("linux"));
    return tags;
}

bool PluginScraper::supportsType(const QString& type) const
{
    const QString want = normalizePluginType(type);
    for (const QString& t : supportedTypes) {
        if (normalizePluginType(t) == want) return true;
    }
    return false;
}

PluginManifest parsePluginManifest(const QString& payload)
{
    const QJsonObject root =
        QJsonDocument::fromJson(payload.toUtf8()).object();
    PluginManifest manifest;
    manifest.name = root.value(QStringLiteral("name")).toString();
    if (manifest.name.trimmed().isEmpty())
        throw ManifestError("Manifest name is missing.");
    manifest.version = root.value(QStringLiteral("version")).toString();
    if (manifest.version.trimmed().isEmpty())
        throw ManifestError("Manifest version is missing.");
    manifest.description = root.value(QStringLiteral("description")).toString();
    manifest.author = root.value(QStringLiteral("author")).toString();
    for (const QJsonValue& v :
         root.value(QStringLiteral("scrapers")).toArray()) {
        const QJsonObject o = v.toObject();
        PluginManifest::Scraper s;
        s.id = o.value(QStringLiteral("id")).toString();
        s.name = o.value(QStringLiteral("name")).toString();
        s.description = o.value(QStringLiteral("description")).toString();
        s.version = o.value(QStringLiteral("version")).toString();
        s.filename = o.value(QStringLiteral("filename")).toString();
        if (o.contains(QStringLiteral("supportedTypes")))
            s.supportedTypes = stringList(o.value(QStringLiteral("supportedTypes")));
        if (o.contains(QStringLiteral("enabled")))
            s.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        s.hasSettings = o.value(QStringLiteral("hasSettings")).toBool(false);
        s.logo = o.value(QStringLiteral("logo")).toString();
        s.contentLanguage =
            stringList(o.value(QStringLiteral("contentLanguage")));
        s.supportedPlatforms =
            stringList(o.value(QStringLiteral("supportedPlatforms")));
        s.disabledPlatforms =
            stringList(o.value(QStringLiteral("disabledPlatforms")));
        const QStringList formats = stringList(o.value(QStringLiteral("formats")));
        const QStringList supportedFormats =
            stringList(o.value(QStringLiteral("supportedFormats")));
        s.formats = !formats.isEmpty() ? formats : supportedFormats;
        s.supportedFormats = supportedFormats;
        manifest.scrapers.append(s);
    }
    if (manifest.scrapers.isEmpty())
        throw ManifestError("Manifest has no providers.");
    return manifest;
}

PluginRepository::PluginRepository(nuvio::authsync::AuthService* auth,
                                   nuvio::tmdb::TmdbService* tmdb,
                                   QObject* parent)
    : QObject(parent),
      m_auth(auth),
      m_tmdb(tmdb),
      m_profileId(nuvio::settings::ActiveProfile::id())
{
    Q_ASSERT(m_auth);
    m_client = new nuvio::authsync::SyncRpcClient(
        nuvio::authsync::AuthConfig::load(),
        [this] { return m_auth->accessToken(); }, this);
    m_nam = new QNetworkAccessManager(this);
}

QVariantList PluginRepository::repositoriesVariant() const
{
    QVariantList out;
    for (const PluginRepositoryItem& r : m_repos)
        out.append(repoVariant(r));
    return out;
}

QVariantList PluginRepository::scrapersVariant() const
{
    QVariantList out;
    for (const PluginScraper& s : m_scrapers)
        out.append(scraperVariant(s));
    return out;
}

void PluginRepository::initialize()
{
    if (!m_loaded) load();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const PluginRepositoryItem& repo : m_repos) {
        int count = 0;
        for (const PluginScraper& s : m_scrapers) {
            if (s.repositoryUrl == repo.manifestUrl) ++count;
        }
        if (isPluginRepositoryRefreshDue(repo.lastUpdated, now) ||
            count < repo.scraperCount)
            fetchManifest(repo.manifestUrl, FetchReason::AutoRefresh, {});
    }
}

void PluginRepository::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    ++m_token;   // drops in-flight refreshes
    m_profileId = profileId;
    m_loaded = false;
    m_pulledFromServer = false;
    m_refreshing.clear();
    load();
}

void PluginRepository::clearLocalState()
{
    ++m_token;
    m_profileId = 1;
    m_loaded = false;
    m_pulledFromServer = false;
    m_refreshing.clear();
    m_pluginsEnabled = true;
    m_groupByRepo = false;
    m_repos.clear();
    m_scrapers.clear();
    emit changed();
}

void PluginRepository::load()
{
    m_loaded = true;
    m_repos.clear();
    m_scrapers.clear();
    m_pluginsEnabled = true;
    m_groupByRepo = false;
    auto store = openStore();
    const auto raw = store.getString(stateKey(m_profileId).toStdString());
    if (!raw || raw->empty()) {
        emit changed();
        return;
    }
    const QJsonObject root =
        QJsonDocument::fromJson(QByteArray::fromStdString(*raw)).object();
    if (root.isEmpty()) {
        emit changed();
        return;
    }
    m_pluginsEnabled =
        root.value(QStringLiteral("pluginsEnabled")).toBool(true);
    m_groupByRepo =
        root.value(QStringLiteral("groupStreamsByRepository")).toBool(false);
    for (const QJsonValue& v :
         root.value(QStringLiteral("repositories")).toArray()) {
        const QJsonObject o = v.toObject();
        PluginRepositoryItem r;
        r.manifestUrl = o.value(QStringLiteral("manifestUrl")).toString();
        if (r.manifestUrl.isEmpty()) continue;
        r.name = o.value(QStringLiteral("name")).toString();
        r.description = o.value(QStringLiteral("description")).toString();
        r.version = o.value(QStringLiteral("version")).toString();
        r.scraperCount = o.value(QStringLiteral("scraperCount")).toInt(0);
        r.lastUpdated =
            qint64(o.value(QStringLiteral("lastUpdated")).toDouble(0));
        m_repos.append(r);
    }
    bool migrated = false;
    for (const QJsonValue& v :
         root.value(QStringLiteral("scrapers")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        // Code resolves from the cache, else the legacy inline copy
        // (migration: re-persisted code-free below).
        QString code = loadScraperCode(id);
        if (code.isEmpty()) {
            code = o.value(QStringLiteral("code")).toString();
            if (!code.isEmpty()) migrated = true;
        }
        if (code.isEmpty()) continue;   // restorePluginScraper parity
        PluginScraper s;
        s.id = id;
        s.repositoryUrl = o.value(QStringLiteral("repositoryUrl")).toString();
        s.name = o.value(QStringLiteral("name")).toString();
        s.description = o.value(QStringLiteral("description")).toString();
        s.version = o.value(QStringLiteral("version")).toString();
        s.filename = o.value(QStringLiteral("filename")).toString();
        s.supportedTypes = stringList(o.value(QStringLiteral("supportedTypes")));
        if (s.supportedTypes.isEmpty())
            s.supportedTypes = {QStringLiteral("movie"),
                                QStringLiteral("tv")};
        s.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        s.manifestEnabled =
            o.value(QStringLiteral("manifestEnabled")).toBool(true);
        s.hasSettings = o.value(QStringLiteral("hasSettings")).toBool(false);
        s.logo = o.value(QStringLiteral("logo")).toString();
        s.contentLanguage =
            stringList(o.value(QStringLiteral("contentLanguage")));
        s.formats = stringList(o.value(QStringLiteral("formats")));
        s.code = code;
        m_scrapers.append(s);
    }
    emit changed();
    if (migrated) persist();
}

void PluginRepository::persist()
{
    QJsonArray repos;
    for (const PluginRepositoryItem& r : m_repos) {
        repos.append(QJsonObject{
            {QStringLiteral("manifestUrl"), r.manifestUrl},
            {QStringLiteral("name"), r.name},
            {QStringLiteral("description"), r.description},
            {QStringLiteral("version"), r.version},
            {QStringLiteral("scraperCount"), r.scraperCount},
            {QStringLiteral("lastUpdated"), double(r.lastUpdated)},
        });
    }
    QJsonArray scrapers;
    for (const PluginScraper& s : m_scrapers) {
        // Code persisted under its own key (overwrite=false: first
        // write wins, fork parity); the state row stays code-free.
        saveScraperCode(s.id, s.code, false);
        QJsonArray types;
        for (const QString& t : s.supportedTypes) types.append(t);
        QJsonArray langs;
        for (const QString& l : s.contentLanguage) langs.append(l);
        QJsonArray formats;
        for (const QString& f : s.formats) formats.append(f);
        scrapers.append(QJsonObject{
            {QStringLiteral("id"), s.id},
            {QStringLiteral("repositoryUrl"), s.repositoryUrl},
            {QStringLiteral("name"), s.name},
            {QStringLiteral("description"), s.description},
            {QStringLiteral("version"), s.version},
            {QStringLiteral("filename"), s.filename},
            {QStringLiteral("supportedTypes"), types},
            {QStringLiteral("enabled"), s.enabled},
            {QStringLiteral("manifestEnabled"), s.manifestEnabled},
            {QStringLiteral("hasSettings"), s.hasSettings},
            {QStringLiteral("logo"), s.logo},
            {QStringLiteral("contentLanguage"), langs},
            {QStringLiteral("formats"), formats},
            {QStringLiteral("code"), QJsonValue::Null},
        });
    }
    auto store = openStore();
    store.putString(
        stateKey(m_profileId).toStdString(),
        QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("pluginsEnabled"), m_pluginsEnabled},
            {QStringLiteral("groupStreamsByRepository"), m_groupByRepo},
            {QStringLiteral("repositories"), repos},
            {QStringLiteral("scrapers"), scrapers},
        }).toJson(QJsonDocument::Compact))
            .toStdString());
}

void PluginRepository::saveScraperCode(const QString& scraperId,
                                       const QString& code, bool overwrite)
{
    if (code.isEmpty()) return;
    auto store = openStore();
    const std::string key = codeKey(m_profileId, scraperId).toStdString();
    if (!overwrite && store.contains(key)) return;
    store.putString(key, code.toStdString());
}

QString PluginRepository::loadScraperCode(const QString& scraperId) const
{
    auto store = openStore();
    const auto raw =
        store.getString(codeKey(m_profileId, scraperId).toStdString());
    return raw ? QString::fromStdString(*raw) : QString();
}

void PluginRepository::pullFromServer()
{
    if (!m_loaded) load();
    if (!m_auth->sessionActive()) return;
    const quint64 token = ++m_token;
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [this, con, token](bool ok, int,
                                      const QJsonDocument& doc, QByteArray) {
                       disconnect(*con);
                       if (token != m_token) return;
                       if (!ok) return;   // silent (fork logs only)
                       QStringList urls;
                       const QJsonArray rows =
                           doc.isArray() ? doc.array() : QJsonArray{};
                       for (const QJsonValue& v : rows) {
                           const QString url = v.toObject()
                                                   .value(QStringLiteral("url"))
                                                   .toString()
                                                   .trimmed();
                           if (url.isEmpty()) continue;
                           QString normalized =
                               normalizeManifestUrl(url);
                           if (!normalized.isEmpty() &&
                               !urls.contains(normalized))
                               urls.append(normalized);
                       }
                       const qint64 now =
                           QDateTime::currentMSecsSinceEpoch();
                       QList<PluginRepositoryItem> nextRepos;
                       for (const QString& url : urls) {
                           auto existing = std::find_if(
                               m_repos.begin(), m_repos.end(),
                               [&](const PluginRepositoryItem& r) {
                                   return r.manifestUrl == url;
                               });
                           if (existing == m_repos.end()) {
                               PluginRepositoryItem fresh;
                               fresh.manifestUrl = url;
                               fresh.name = url.section(u'?', 0, 0)
                                                .section(u'/', -1);
                               fresh.isRefreshing = true;
                               nextRepos.append(fresh);
                           } else {
                               int count = 0;
                               for (const PluginScraper& s : m_scrapers) {
                                   if (s.repositoryUrl == url) ++count;
                               }
                               existing->isRefreshing =
                                   isPluginRepositoryRefreshDue(
                                       existing->lastUpdated, now) ||
                                   count < existing->scraperCount;
                               if (existing->isRefreshing)
                                   existing->errorMessage.clear();
                               nextRepos.append(*existing);
                           }
                       }
                       QList<PluginScraper> nextScrapers;
                       for (const PluginScraper& s : m_scrapers) {
                           if (urls.contains(s.repositoryUrl))
                               nextScrapers.append(s);
                       }
                       // Empty server + never-pulled + local rows seeds
                       // the server from local state (fork parity).
                       if (urls.isEmpty() && !m_pulledFromServer) {
                           m_pulledFromServer = true;
                           if (!m_repos.isEmpty()) pushToServer();
                           return;
                       }
                       m_pulledFromServer = true;
                       m_repos = nextRepos;
                       m_scrapers = nextScrapers;
                       persist();
                       emit changed();
                       for (const PluginRepositoryItem& r : m_repos) {
                           if (r.isRefreshing)
                               fetchManifest(r.manifestUrl,
                                             FetchReason::AutoRefresh, {});
                       }
                   });
    // Empty local + empty server seeds the server from local state
    // (fork parity): handled after the select answers.
    m_client->get(QStringLiteral("plugins?profile_id=eq.%1&select=url,name,"
                                 "enabled&order=sort_order")
                      .arg(m_profileId));
}

void PluginRepository::pushToServer()
{
    if (!m_auth->sessionActive()) return;
    QJsonArray plugins;
    int order = 0;
    for (const PluginRepositoryItem& r : m_repos) {
        plugins.append(QJsonObject{
            {QStringLiteral("url"), r.manifestUrl},
            {QStringLiteral("name"), r.name},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("sort_order"), order++},
        });
    }
    auto con = std::make_shared<QMetaObject::Connection>();
    *con = connect(m_client, &nuvio::authsync::SyncRpcClient::finished, this,
                   [con](bool, int, const QJsonDocument&, QByteArray) {
                       disconnect(*con);   // fire-and-forget (fork parity)
                   });
    m_client->call(QStringLiteral("sync_push_plugins"),
                   QJsonObject{
                       {QStringLiteral("p_profile_id"), m_profileId},
                       {QStringLiteral("p_plugins"), plugins},
                   });
}

void PluginRepository::addRepository(const QString& rawUrl)
{
    if (!m_loaded) load();
    QString error;
    const QString manifestUrl = normalizeManifestUrl(rawUrl, &error);
    if (manifestUrl.isEmpty()) {
        emit addRepositoryFinished(false, error);
        return;
    }
    for (const PluginRepositoryItem& r : m_repos) {
        if (r.manifestUrl == manifestUrl) {
            emit addRepositoryFinished(
                false,
                QStringLiteral("That plugin repository is already "
                               "installed."));
            return;
        }
    }
    QHash<QString, PluginScraper> previous;
    for (const PluginScraper& s : m_scrapers) previous.insert(s.id, s);
    fetchManifest(manifestUrl, FetchReason::Add, previous);
}

void PluginRepository::removeRepository(const QString& manifestUrl)
{
    if (!m_loaded) load();
    m_repos.erase(std::remove_if(m_repos.begin(), m_repos.end(),
                                 [&](const PluginRepositoryItem& r) {
                                     return r.manifestUrl == manifestUrl;
                                 }),
                  m_repos.end());
    m_scrapers.erase(std::remove_if(m_scrapers.begin(), m_scrapers.end(),
                                    [&](const PluginScraper& s) {
                                        return s.repositoryUrl == manifestUrl;
                                    }),
                     m_scrapers.end());
    persist();
    emit changed();
    pushToServer();
}

void PluginRepository::refreshAll()
{
    if (!m_loaded) load();
    for (const PluginRepositoryItem& r : m_repos)
        fetchManifest(r.manifestUrl, FetchReason::AutoRefresh, {});
}

void PluginRepository::refreshRepository(const QString& manifestUrl)
{
    if (!m_loaded) load();
    fetchManifest(manifestUrl, FetchReason::UserRefresh, {});
}

void PluginRepository::toggleScraper(const QString& scraperId, bool enabled)
{
    if (!m_loaded) load();
    for (PluginScraper& s : m_scrapers) {
        if (s.id != scraperId) continue;
        // Manifest-disabled scrapers stay off (fork parity).
        s.enabled = s.manifestEnabled && enabled;
    }
    persist();
    emit changed();
}

void PluginRepository::setPluginsEnabled(bool enabled)
{
    if (!m_loaded) load();
    if (m_pluginsEnabled == enabled) return;
    m_pluginsEnabled = enabled;
    persist();
    emit changed();
}

void PluginRepository::setGroupStreamsByRepository(bool enabled)
{
    if (!m_loaded) load();
    if (m_groupByRepo == enabled) return;
    m_groupByRepo = enabled;
    persist();
    emit changed();
}

QList<PluginScraper> PluginRepository::enabledScrapersForType(
    const QString& type)
{
    if (!m_loaded) load();
    if (!m_pluginsEnabled) return {};
    QList<PluginScraper> out;
    for (const PluginScraper& s : m_scrapers) {
        if (s.enabled && s.supportsType(type)) out.append(s);
    }
    return out;
}

QVariantList PluginRepository::enabledScrapers(const QString& type)
{
    QVariantList out;
    for (const PluginScraper& s : enabledScrapersForType(type))
        out.append(scraperVariant(s));
    return out;
}

void PluginRepository::fetchManifest(
    const QString& manifestUrl, FetchReason reason,
    const QHash<QString, PluginScraper>& previous)
{
    const quint64 token = m_token;
    if (m_refreshing.contains(manifestUrl)) return;
    m_refreshing.insert(manifestUrl);
    // isRefreshing flag on the row (adds show it via the result).
    for (PluginRepositoryItem& r : m_repos) {
        if (r.manifestUrl == manifestUrl) {
            r.isRefreshing = true;
            r.errorMessage.clear();
        }
    }
    emit changed();
    QNetworkReply* rep = m_nam->get(QNetworkRequest{QUrl(manifestUrl)});
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, manifestUrl, reason, previous, token] {
                rep->deleteLater();
                m_refreshing.remove(manifestUrl);
                if (token != m_token) return;
                if (rep->error() != QNetworkReply::NoError) {
                    applyFetchError(manifestUrl, reason,
                                    rep->errorString().isEmpty()
                                        ? QStringLiteral(
                                              "Unable to refresh repository")
                                        : rep->errorString());
                    return;
                }
                PluginManifest manifest;
                try {
                    manifest = parsePluginManifest(
                        QString::fromUtf8(rep->readAll()));
                } catch (const ManifestError& e) {
                    applyFetchError(manifestUrl, reason,
                                    QString::fromUtf8(e.what()));
                    return;
                }
                // Scraper code files fan out, then the row applies.
                QString base = manifestUrl.section(u'?', 0, 0);
                if (base.endsWith(QLatin1String("/manifest.json")))
                    base = base.left(base.size() - 14);
                struct FetchCtx {
                    PluginManifest manifest;
                    QList<PluginScraper> scrapers;
                    int pending = 0;
                };
                auto* fctx = new FetchCtx{manifest, {}, 0};
                const QStringList tags = currentPluginPlatformTags();
                QList<PluginManifest::Scraper> wanted;
                for (const auto& info : manifest.scrapers) {
                    const QStringList supported =
                        toLowerList(info.supportedPlatforms);
                    const QStringList disabled =
                        toLowerList(info.disabledPlatforms);
                    if (!supported.isEmpty()) {
                        bool any = false;
                        for (const QString& t : tags) {
                            if (supported.contains(t)) {
                                any = true;
                                break;
                            }
                        }
                        if (!any) continue;
                    }
                    bool blocked = false;
                    for (const QString& t : tags) {
                        if (disabled.contains(t)) {
                            blocked = true;
                            break;
                        }
                    }
                    if (blocked) continue;
                    wanted.append(info);
                }
                if (wanted.isEmpty()) {
                    PluginRepositoryItem repo;
                    repo.manifestUrl = manifestUrl;
                    repo.name = manifest.name;
                    repo.description = manifest.description;
                    repo.version = manifest.version;
                    repo.scraperCount = 0;
                    repo.lastUpdated =
                        QDateTime::currentMSecsSinceEpoch();
                    applyFetched(manifestUrl, reason, repo, {});
                    delete fctx;
                    return;
                }
                fctx->pending = wanted.size();
                for (const auto& info : wanted) {
                    QString codeUrl = info.filename;
                    if (!codeUrl.startsWith(QLatin1String("http://")) &&
                        !codeUrl.startsWith(QLatin1String("https://"))) {
                        QString file = info.filename;
                        while (file.startsWith(u'/')) file = file.mid(1);
                        codeUrl = base + u'/' + file;
                    }
                    const QString scraperId =
                        manifestUrl.toLower() + u':' + info.id;
                    QNetworkReply* codeRep =
                        m_nam->get(QNetworkRequest{QUrl(codeUrl)});
                    connect(codeRep, &QNetworkReply::finished, this,
                            [this, codeRep, manifestUrl, reason, previous,
                             token, fctx, info, scraperId] {
                                codeRep->deleteLater();
                                if (token == m_token &&
                                    codeRep->error() ==
                                        QNetworkReply::NoError) {
                                    const QString code = QString::fromUtf8(
                                        codeRep->readAll());
                                    saveScraperCode(scraperId, code, true);
                                    const auto prevIt =
                                        previous.constFind(scraperId);
                                    const PluginScraper* prev =
                                        prevIt != previous.constEnd()
                                            ? &prevIt.value()
                                            : nullptr;
                                    PluginScraper scraper;
                                    scraper.id = scraperId;
                                    scraper.repositoryUrl = manifestUrl;
                                    scraper.name = info.name;
                                    scraper.description = info.description;
                                    scraper.version = info.version;
                                    scraper.filename = info.filename;
                                    scraper.supportedTypes =
                                        info.supportedTypes.isEmpty()
                                            ? QStringList{
                                                  QStringLiteral("movie"),
                                                  QStringLiteral("tv")}
                                            : info.supportedTypes;
                                    scraper.manifestEnabled = info.enabled;
                                    scraper.enabled =
                                        !info.enabled
                                            ? false
                                            : (prev ? prev->enabled
                                                    : info.enabled);
                                    scraper.hasSettings = info.hasSettings;
                                    scraper.logo = info.logo;
                                    scraper.contentLanguage =
                                        info.contentLanguage;
                                    scraper.formats =
                                        !info.formats.isEmpty()
                                            ? info.formats
                                            : info.supportedFormats;
                                    scraper.code = code;
                                    fctx->scrapers.append(scraper);
                                }
                                if (--fctx->pending == 0) {
                                    PluginRepositoryItem repo;
                                    repo.manifestUrl = manifestUrl;
                                    repo.name = fctx->manifest.name;
                                    repo.description =
                                        fctx->manifest.description;
                                    repo.version = fctx->manifest.version;
                                    repo.scraperCount =
                                        fctx->scrapers.size();
                                    repo.lastUpdated = QDateTime::
                                        currentMSecsSinceEpoch();
                                    if (token == m_token)
                                        applyFetched(manifestUrl, reason, repo,
                                                     fctx->scrapers);
                                    delete fctx;
                                }
                            });
                }
            });
}

void PluginRepository::applyFetched(
    const QString& manifestUrl, FetchReason reason,
    const PluginRepositoryItem& repo,
    const QList<PluginScraper>& scrapers)
{
    bool replaced = false;
    for (PluginRepositoryItem& r : m_repos) {
        if (r.manifestUrl == manifestUrl) {
            r = repo;
            replaced = true;
        }
    }
    if (!replaced) m_repos.append(repo);
    m_scrapers.erase(std::remove_if(m_scrapers.begin(), m_scrapers.end(),
                                    [&](const PluginScraper& s) {
                                        return s.repositoryUrl == manifestUrl;
                                    }),
                     m_scrapers.end());
    for (const PluginScraper& s : scrapers) m_scrapers.append(s);
    persist();
    emit changed();
    // Push policy (fork parity): adds push unconditionally (server
    // rows are keyed by install), user refreshes push; background
    // auto-refreshes stay quiet.
    if (reason == FetchReason::Add) {
        emit addRepositoryFinished(true, repo.name);
        pushToServer();
    } else if (reason == FetchReason::UserRefresh) {
        pushToServer();
    }
}

void PluginRepository::applyFetchError(const QString& manifestUrl,
                                       FetchReason reason,
                                       const QString& message)
{
    if (reason == FetchReason::Add) {
        emit addRepositoryFinished(false, message);
        return;
    }
    for (PluginRepositoryItem& r : m_repos) {
        if (r.manifestUrl == manifestUrl) {
            r.isRefreshing = false;
            r.errorMessage = message.isEmpty()
                                 ? QStringLiteral("Unable to refresh repository")
                                 : message;
        }
    }
    persist();
    emit changed();
}

void PluginRepository::testScraper(const QString& scraperId,
                                   TestCallback done)
{
    if (!m_loaded) load();
    const PluginScraper* found = nullptr;
    for (const PluginScraper& s : m_scrapers) {
        if (s.id == scraperId) {
            found = &s;
            break;
        }
    }
    if (!found) {
        done({}, QStringLiteral("Provider not found"));
        emit testFinished(scraperId, QVariantList{},
                          QStringLiteral("Provider not found"));
        return;
    }
    const PluginScraper scraper = *found;
    const QString mediaType =
        scraper.supportsType(QStringLiteral("movie"))
            ? QStringLiteral("movie")
            : QStringLiteral("tv");
    const int season = mediaType == QLatin1String("tv") ? 1 : -1;
    const int episode = mediaType == QLatin1String("tv") ? 1 : -1;
    auto* self = this;
    executeScraper(scraper, QStringLiteral("603"), mediaType, season,
                   episode,
                   [self, scraperId, done = std::move(done)](
                       const QList<PluginStreamResult>& rows) {
                       QVariantList out;
                       for (const PluginStreamResult& r : rows)
                           out.append(pluginResultToMap(r));
                       done(rows, QString());
                       emit self->testFinished(scraperId, out, QString());
                   });
}

void PluginRepository::executeScraper(const PluginScraper& scraper,
                                      const QString& tmdbId,
                                      const QString& mediaType, int season,
                                      int episode, ExecCallback done)
{
    // Tmdb-id resolution (fork resolvePluginTmdbId parity): the A5
    // service maps foreign ids, falling back to the input id.
    const QString type = normalizePluginType(mediaType);
    const QString trimmed = tmdbId.trimmed();
    auto run = [this, scraper, type, season, episode, trimmed,
                done = std::move(done)](const QString& resolvedId) {
        auto store = openStore();
        const auto raw =
            store.getString(settingsKey(scraper.id).toStdString());
        const QString settings =
            raw ? QString::fromStdString(*raw) : QStringLiteral("{}");
        // One runtime per execution (each owns a worker thread): the
        // batch wavefront caps live threads; the runtime dies with the
        // callback chain (fork parity: fresh engine per execution).
        auto* runtime = new PluginRuntime;
        runtime->execute(
            scraper.code, resolvedId.isEmpty() ? trimmed : resolvedId, type,
            season, episode, scraper.id, settings,
            [runtime, done = std::move(done)](
                const QList<PluginStreamResult>& rows, const QString&) {
                done(rows);
                runtime->deleteLater();
            });
    };
    if (trimmed.isEmpty() || !m_tmdb) {
        run(trimmed);
        return;
    }
    m_tmdb->ensureTmdbId(trimmed, type,
                         [run = std::move(run),
                          trimmed](const QString& mapped) {
                             run(mapped.isEmpty() ? trimmed : mapped);
                         });
}

void PluginRepository::executeFor(const QString& mediaType,
                                  const QString& contentId, int season,
                                  int episode, ExecCallback done)
{
    if (!m_loaded) load();
    QList<PluginScraper> targets;
    for (const PluginScraper& s : m_scrapers) {
        if (s.enabled && s.supportsType(mediaType)) targets.append(s);
    }
    if (targets.isEmpty() || !m_pluginsEnabled) {
        done({});
        return;
    }
    // One runtime per in-flight scraper (each owns a worker thread);
    // waves of 4 bound concurrency (fork launches uncapped - the
    // throttle only shapes latency, merged rows are identical).
    struct Batch {
        QList<QList<PluginStreamResult>> parts;
        int pending = 0;
        int inFlight = 0;
        int nextWave = 0;
        ExecCallback done;
    };
    auto* batch = new Batch;
    batch->pending = targets.size();
    batch->done = std::move(done);
    const QString content = pluginContentId(contentId, season, episode);
    auto launchWave = std::make_shared<std::function<void()>>();
    *launchWave = [this, targets, content, mediaType, season, episode, batch,
                   launchWave]() mutable {
        while (batch->inFlight < kMaxParallelExecutions &&
               batch->nextWave < targets.size()) {
            const int i = batch->nextWave++;
            ++batch->inFlight;
            executeScraper(targets[i], content, mediaType, season, episode,
                           [batch, launchWave](
                               const QList<PluginStreamResult>& rows) {
                               batch->parts.append(rows);
                               --batch->inFlight;
                               if (--batch->pending == 0) {
                                   QList<PluginStreamResult> merged;
                                   for (const auto& part : batch->parts) {
                                       for (const auto& r : part)
                                           merged.append(r);
                                   }
                                   auto done = std::move(batch->done);
                                   delete batch;
                                   done(merged);
                               } else {
                                   (*launchWave)();
                               }
                           });
        }
    };
    (*launchWave)();
}

void PluginRepository::testScraper(const QString& scraperId)
{
    testScraper(scraperId,
                [](const QList<PluginStreamResult>&, const QString&) {});
}

void PluginRepository::requestSettingsLayout(const QString& scraperId)
{
    settingsLayout(scraperId,
                   [this, scraperId](const QString& layout) {
                       emit settingsLayoutReady(scraperId, layout);
                   });
}

void PluginRepository::settingsLayout(const QString& scraperId,
                                      LayoutCallback done)
{
    if (!m_loaded) load();
    for (const PluginScraper& s : m_scrapers) {
        if (s.id != scraperId) continue;
        auto* runtime = new PluginRuntime;
        runtime->settingsLayout(
            s.code, s.id,
            [runtime, done = std::move(done)](const QString& layout) {
                done(layout);
                runtime->deleteLater();
            });
        return;
    }
    done(QStringLiteral("[]"));
}

QString PluginRepository::loadScraperSettings(const QString& scraperId)
{
    auto store = openStore();
    const auto raw =
        store.getString(settingsKey(scraperId).toStdString());
    return raw ? QString::fromStdString(*raw) : QStringLiteral("{}");
}

void PluginRepository::saveScraperSettings(const QString& scraperId,
                                           const QString& payload)
{
    auto store = openStore();
    store.putString(settingsKey(scraperId).toStdString(),
                    payload.toStdString());
}

} // namespace nuvio::plugins
