#pragma once

// Plugin execution (fork PluginRuntime parity): runs scraper code
// through the hosted engine (polyfill + bridges), calls getStreams /
// onSettings, captures the JSON result. Async on a worker thread with
// the 60s budget; results parse verbatim (url required, [object
// guard, header/subtitle shapes).

#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace nuvio::plugins {

/// mediaType normalization ("series"/"show"/"other" -> "tv").
[[nodiscard]] QString normalizePluginType(const QString& type);

struct PluginStreamResult {
    QString title;
    QString name;
    QString url;
    QString quality;
    QString size;
    QString language;
    QString provider;
    QString type;
    int seeders = -1;
    int peers = -1;
    QString infoHash;
    QVariantMap headers;
    QVariantList subtitles;   // [{url,language,name,headers}]
};

/// Parses the captured getStreams JSON (empty on any malformation,
///
/// fork parseJsonResults parity).
[[nodiscard]] QList<PluginStreamResult> parsePluginResults(
    const QString& rawJson);
[[nodiscard]] QVariantMap pluginResultToMap(const PluginStreamResult& r);

class PluginRuntime final : public QObject {
    Q_OBJECT

public:
    using ResultsCallback =
        std::function<void(const QList<PluginStreamResult>& rows,
                           const QString& error)>;
    using LayoutCallback =
        std::function<void(const QString& layoutJson)>;

    explicit PluginRuntime(QObject* parent = nullptr);
    ~PluginRuntime() override;

    /// Executes one scraper (60s budget). The callback fires exactly
    /// once, on the caller's thread.
    void execute(const QString& code, const QString& tmdbId,
                 const QString& mediaType, int season, int episode,
                 const QString& scraperId, const QString& settingsJson,
                 ResultsCallback done);
    /// Resolves the onSettings layout JSON ("[]" when absent).
    void settingsLayout(const QString& code, const QString& scraperId,
                        LayoutCallback done);

    /// Test seam: synchronous execution on the calling thread (pumps
    /// its event loop; never use from the UI thread in production).
    QList<PluginStreamResult> executeSync(
        const QString& code, const QString& tmdbId, const QString& mediaType,
        int season, int episode, const QString& scraperId,
        const QString& settingsJson, QString* errorOut = nullptr,
        int timeoutMs = 60000);

private:
    struct Job;
    // Runs on the worker thread (single-threaded engine contract).
    void runJob(Job* job);

    QThread m_worker;
    QObject* m_actor = nullptr;
};

} // namespace nuvio::plugins
