#pragma once

// Plugin host (fork PluginRuntime bridge-registration parity): owns one
// JsEngine + QNAM + DomBridge, registers console/capture/url/fetch/
// crypto/cheerio globals for a scraper run, and pumps the Qt event loop
// together with the JS job queue so async fetch promises settle.
// Single-threaded by contract (the engine lives on the caller's thread).

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariant>

#include <functional>

#include "nuvio/plugins/DomBridge.h"
#include "nuvio/plugins/JsEngine.h"

namespace nuvio::plugins {

class PluginHost final : public QObject {
    Q_OBJECT

public:
    using Logger = std::function<void(const QString& line)>;
    using ResultCallback = std::function<void(const QString& json)>;

    explicit PluginHost(QObject* parent = nullptr);

    [[nodiscard]] JsEngine* engine() { return &m_engine; }
    void setLogger(Logger logger) { m_logger = std::move(logger); }

    /// Loads the verbatim polyfill (scraper id + settings JSON) and
    /// registers every host bridge. onResult receives the getStreams
    /// capture; onSettings the onSettings capture (may both stay empty
    /// on failure - callers apply fork defaults).
    bool setup(const QString& scraperId, const QString& settingsJson,
               ResultCallback onResult, ResultCallback onSettings,
               QString* errorOut = nullptr);

    /// Pumps Qt events + JS jobs until done() or the deadline (ms).
    /// Returns false on timeout or job failure (errorOut set).
    bool pumpUntil(const std::function<bool()>& done, int timeoutMs,
                   QString* errorOut = nullptr);

    [[nodiscard]] static QString loadPolyfill(const QString& scraperIdJson,
                                              const QString& settingsJson);

private:
    void registerConsole(const QString& scraperId);
    void registerUrl();
    void registerFetch();
    void registerCrypto();
    void bindDom();

    JsEngine m_engine;
    QNetworkAccessManager* m_nam = nullptr;
    DomBridge m_dom;
    Logger m_logger;
    ResultCallback m_onResult;
    ResultCallback m_onSettings;
};

} // namespace nuvio::plugins
