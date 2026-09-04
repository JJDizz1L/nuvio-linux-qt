#pragma once

// Plugin repository (fork fullCommonMain PluginRepository parity):
// manifest fetch/refresh, scraper enablement, per-profile state +
// cached scraper code, scraper settings, test + parallel execution,
// server push/pull. Qt simplifications (documented): persistence is
// direct (the fork's generation/revision machinery guards coroutine
// races this single-threaded port cannot produce); platform tags are
// {desktop,qt,<os>} — the fork's "jvm" tag is not claimed (no JVM
// here), so jvm-gated manifests honestly skip.

#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <stdexcept>

#include "nuvio/plugins/PluginRuntime.h"

namespace nuvio::authsync {
class AuthService;
class SyncRpcClient;
} // namespace nuvio::authsync

namespace nuvio::tmdb {
class TmdbService;
}

namespace nuvio::plugins {

/// Manifest URL normalization (scheme default, fragment strip,
/// /manifest.json suffix, unsafe-char encoding). Empty when blank.
[[nodiscard]] QString normalizeManifestUrl(const QString& rawUrl,
                                           QString* errorOut = nullptr);
[[nodiscard]] QString encodeUnsafeHttpUrlCharacters(const QString& value);
/// "tt123" content id for plugin calls (tmdb: prefix + :S:E suffix
/// stripped, fork pluginContentId parity).
[[nodiscard]] QString pluginContentId(const QString& videoId, int season,
                                      int episode);
/// 6h refresh staleness (+ scraper-count shortfall handled by caller).
[[nodiscard]] bool isPluginRepositoryRefreshDue(qint64 lastUpdatedEpochMs,
                                                qint64 nowEpochMs);
/// Current platform tags, lowercased.
[[nodiscard]] QStringList currentPluginPlatformTags();

struct PluginRepositoryItem {
    QString manifestUrl;
    QString name;
    QString description;
    QString version;
    int scraperCount = 0;
    qint64 lastUpdated = 0;
    bool isRefreshing = false;
    QString errorMessage;
};

struct PluginScraper {
    QString id;   // "<manifest-lower>:<scraper-id>"
    QString repositoryUrl;
    QString name;
    QString description;
    QString version;
    QString filename;
    QStringList supportedTypes;
    bool enabled = true;
    bool manifestEnabled = true;
    bool hasSettings = false;
    QString logo;
    QStringList contentLanguage;
    QStringList formats;
    QStringList supportedFormats;
    QString code;

    [[nodiscard]] bool supportsType(const QString& type) const;
};

struct PluginManifest {
    QString name;
    QString version;
    QString description;
    QString author;
    struct Scraper {
        QString id;
        QString name;
        QString description;
        QString version;
        QString filename;
        QStringList supportedTypes = {QStringLiteral("movie"),
                                      QStringLiteral("tv")};
        bool enabled = true;
        bool hasSettings = false;
        QString logo;
        QStringList contentLanguage;
        QStringList supportedPlatforms;
        QStringList disabledPlatforms;
        QStringList formats;
        QStringList supportedFormats;
    };
    QList<Scraper> scrapers;
};

/// Strict manifest parse (name/version/scrapers required, fork
/// PluginManifestParser parity). Throws ManifestError on violation.
struct ManifestError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
[[nodiscard]] PluginManifest parsePluginManifest(const QString& payload);

class PluginRepository final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool pluginsEnabled READ pluginsEnabled NOTIFY changed)
    Q_PROPERTY(bool groupStreamsByRepository READ groupStreamsByRepository
                   NOTIFY changed)
    Q_PROPERTY(QVariantList repositories READ repositoriesVariant
                   NOTIFY changed)
    Q_PROPERTY(QVariantList scrapers READ scrapersVariant NOTIFY changed)

public:
    using AddCallback =
        std::function<void(bool ok, const QString& message)>;
    using TestCallback =
        std::function<void(const QList<PluginStreamResult>& rows,
                           const QString& error)>;
    using ExecCallback =
        std::function<void(const QList<PluginStreamResult>& rows)>;
    using LayoutCallback = std::function<void(const QString& layoutJson)>;

    explicit PluginRepository(nuvio::authsync::AuthService* auth,
                              nuvio::tmdb::TmdbService* tmdb,
                              QObject* parent = nullptr);

    [[nodiscard]] bool pluginsEnabled() const { return m_pluginsEnabled; }
    [[nodiscard]] bool groupStreamsByRepository() const
    {
        return m_groupByRepo;
    }
    [[nodiscard]] QVariantList repositoriesVariant() const;
    [[nodiscard]] QVariantList scrapersVariant() const;
    [[nodiscard]] QList<PluginRepositoryItem> repositories() const
    {
        return m_repos;
    }
    [[nodiscard]] QList<PluginScraper> scrapers() const { return m_scrapers; }

    Q_INVOKABLE void initialize();
    /// Profile switches (P7): drops in-flight refreshes, reloads.
    Q_INVOKABLE void setProfileId(int profileId);
    Q_INVOKABLE void clearLocalState();

    Q_INVOKABLE void pullFromServer();
    Q_INVOKABLE void addRepository(const QString& rawUrl);
    Q_INVOKABLE void removeRepository(const QString& manifestUrl);
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void refreshRepository(const QString& manifestUrl);
    Q_INVOKABLE void toggleScraper(const QString& scraperId, bool enabled);
    Q_INVOKABLE void setPluginsEnabled(bool enabled);
    Q_INVOKABLE void setGroupStreamsByRepository(bool enabled);

    /// Enabled scrapers for a content type (C++ surface for the
    /// streams tier; empty when disabled).
    [[nodiscard]] QList<PluginScraper> enabledScrapersForType(
        const QString& type);
    Q_INVOKABLE QVariantList enabledScrapers(const QString& type);

    /// Trial run (tmdb 603, S1E1 for series). Callback on caller thread.
    void testScraper(const QString& scraperId, TestCallback done);
    /// QML entry: result arrives via testFinished.
    Q_INVOKABLE void testScraper(const QString& scraperId);    /// Single execution with tmdb-id resolution. Callback on caller.
    void executeScraper(const PluginScraper& scraper, const QString& tmdbId,
                        const QString& mediaType, int season, int episode,
                        ExecCallback done);
    /// Parallel fan-out over the enabled scrapers (cap 4 in flight);
    /// merged rows arrive once, on the caller thread.
    void executeFor(const QString& mediaType, const QString& contentId,
                    int season, int episode, ExecCallback done);
    /// onSettings layout JSON ("[]" when absent).
    void settingsLayout(const QString& scraperId, LayoutCallback done);
    /// QML entry: layout arrives via settingsLayoutReady.
    Q_INVOKABLE void requestSettingsLayout(const QString& scraperId);
    Q_INVOKABLE QString loadScraperSettings(const QString& scraperId);
    Q_INVOKABLE void saveScraperSettings(const QString& scraperId,
                                         const QString& payload);

signals:
    void changed();
    void addRepositoryFinished(bool ok, const QString& message);
    void testFinished(const QString& scraperId, const QVariantList& rows,
                      const QString& error);
    void settingsLayoutReady(const QString& scraperId,
                             const QString& layoutJson);

public:
    enum class FetchReason { Add, AutoRefresh, UserRefresh };

private:
    void load();
    void persist();
    void fetchManifest(const QString& manifestUrl, FetchReason reason,
                       const QHash<QString, PluginScraper>& previous);
    void applyFetched(const QString& manifestUrl, FetchReason reason,
                      const PluginRepositoryItem& repo,
                      const QList<PluginScraper>& scrapers);
    void applyFetchError(const QString& manifestUrl, FetchReason reason,
                         const QString& message);
    void pushToServer();
    void saveScraperCode(const QString& scraperId, const QString& code,
                         bool overwrite);
    [[nodiscard]] QString loadScraperCode(const QString& scraperId) const;

    nuvio::authsync::AuthService* m_auth = nullptr;
    nuvio::tmdb::TmdbService* m_tmdb = nullptr;
    nuvio::authsync::SyncRpcClient* m_client = nullptr;
    QNetworkAccessManager* m_nam = nullptr;

    int m_profileId = 1;
    bool m_pluginsEnabled = true;
    bool m_groupByRepo = false;
    bool m_pulledFromServer = false;
    QList<PluginRepositoryItem> m_repos;
    QList<PluginScraper> m_scrapers;
    QSet<QString> m_refreshing;
    bool m_loaded = false;
    quint64 m_token = 0;
};

} // namespace nuvio::plugins
