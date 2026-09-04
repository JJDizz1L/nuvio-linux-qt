#include "nuvio/plugins/PluginRuntime.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "nuvio/plugins/PluginHost.h"

namespace nuvio::plugins {

namespace {

constexpr int kPluginTimeoutMs = 60000;   // fork PLUGIN_TIMEOUT_MS

[[nodiscard]] QString cleanText(const QJsonObject& o, const char* key)
{
    const QString v = o.value(QLatin1String(key)).toString();
    if (v.isEmpty() || v.contains(QLatin1String("[object")))
        return {};
    return v;
}

[[nodiscard]] QVariantMap stringMap(const QJsonObject& o)
{
    QVariantMap out;
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString v = it.value().toString();
        if (!v.isEmpty()) out.insert(it.key(), v);
    }
    return out;
}

[[nodiscard]] QString wrappedCode(const QString& code)
{
    return QStringLiteral("var module = { exports: {} };\n"
                          "var exports = module.exports;\n"
                          "(function() {\n%1\n})();")
        .arg(code);
}

[[nodiscard]] QString getStreamsCall(const QString& tmdbId,
                                     const QString& mediaType, int season,
                                     int episode)
{
    const auto arg = [](QString v) {
        return v.isEmpty() ? QStringLiteral("undefined")
                           : QStringLiteral("'") +
                                 v.replace(u'\'', QStringLiteral("\\'")) +
                                 QStringLiteral("'");
    };
    const QString seasonArg = season < 0 ? QStringLiteral("undefined")
                                         : QString::number(season);
    const QString episodeArg = episode < 0 ? QStringLiteral("undefined")
                                           : QString::number(episode);
    return QStringLiteral(
               "(async function() {\n"
               "  try {\n"
               "    var getStreams = module.exports.getStreams || "
               "globalThis.getStreams;\n"
               "    if (!getStreams) {\n"
               "      console.error('getStreams function not found on "
               "module.exports or globalThis');\n"
               "      __capture_result(JSON.stringify([]));\n"
               "      return;\n"
               "    }\n"
               "    var result = await getStreams(%1, %2, %3, %4);\n"
               "    __capture_result(JSON.stringify(result || []));\n"
               "  } catch (e) {\n"
               "    console.error('getStreams error:', e && e.message ? "
               "e.message : e, e && e.stack ? e.stack : '');\n"
               "    __capture_result(JSON.stringify([]));\n"
               "  }\n"
               "})();")
        .arg(arg(tmdbId), arg(mediaType), seasonArg, episodeArg);
}

[[nodiscard]] QString onSettingsCall()
{
    return QStringLiteral(
        "(async function() {\n"
        "  try {\n"
        "    var onSettings = (typeof module !== 'undefined' && "
        "module.exports && module.exports.onSettings) || "
        "globalThis.onSettings;\n"
        "    if (typeof onSettings === 'function') {\n"
        "      var layout = await onSettings();\n"
        "      __capture_settings_result(JSON.stringify(layout || []));\n"
        "    } else {\n"
        "      __capture_settings_result('[]');\n"
        "    }\n"
        "  } catch (e) {\n"
        "    console.error('onSettings error:', e);\n"
        "    __capture_settings_result('[]');\n"
        "  }\n"
        "})();");
}

} // namespace

QString normalizePluginType(const QString& type)
{
    const QString t = type.trimmed().toLower();
    if (t == QLatin1String("series") || t == QLatin1String("show") ||
        t == QLatin1String("other"))
        return QStringLiteral("tv");
    return t;
}

QList<PluginStreamResult> parsePluginResults(const QString& rawJson)
{
    QList<PluginStreamResult> out;
    const QJsonDocument doc =
        QJsonDocument::fromJson(rawJson.toUtf8());
    if (!doc.isArray()) return out;
    for (const QJsonValue& entry : doc.array()) {
        if (!entry.isObject()) continue;
        const QJsonObject item = entry.toObject();
        // url: plain string or {url} object, non-blank required.
        QString url;
        const QJsonValue urlValue = item.value(QStringLiteral("url"));
        if (urlValue.isString())
            url = urlValue.toString().trimmed();
        else if (urlValue.isObject())
            url = urlValue.toObject()
                      .value(QStringLiteral("url"))
                      .toString()
                      .trimmed();
        if (url.isEmpty()) continue;
        PluginStreamResult r;
        r.title = cleanText(item, "title");
        if (r.title.isEmpty()) r.title = cleanText(item, "name");
        if (r.title.isEmpty()) r.title = QStringLiteral("Unknown");
        r.name = cleanText(item, "name");
        r.url = url;
        r.quality = cleanText(item, "quality");
        r.size = cleanText(item, "size");
        r.language = cleanText(item, "language");
        r.provider = cleanText(item, "provider");
        r.type = cleanText(item, "type");
        const QJsonValue seeders = item.value(QStringLiteral("seeders"));
        r.seeders = seeders.isDouble() ? seeders.toInt(-1) : -1;
        const QJsonValue peers = item.value(QStringLiteral("peers"));
        r.peers = peers.isDouble() ? peers.toInt(-1) : -1;
        r.infoHash = cleanText(item, "infoHash");
        const QJsonValue headers = item.value(QStringLiteral("headers"));
        if (headers.isObject()) {
            const QVariantMap all = stringMap(headers.toObject());
            if (!all.isEmpty()) r.headers = all;
        }
        const QJsonValue subs = item.value(QStringLiteral("subtitles"));
        if (subs.isArray()) {
            QVariantList list;
            for (const QJsonValue& s : subs.toArray()) {
                if (!s.isObject()) continue;
                const QJsonObject sub = s.toObject();
                const QString subUrl =
                    sub.value(QStringLiteral("url")).toString();
                if (subUrl.isEmpty()) continue;
                QString subLang =
                    sub.value(QStringLiteral("language")).toString();
                if (subLang.isEmpty()) subLang = QStringLiteral("Unknown");
                QVariantMap row;
                row.insert(QStringLiteral("url"), subUrl);
                row.insert(QStringLiteral("language"), subLang);
                const QString subName = cleanText(sub, "name");
                if (!subName.isEmpty())
                    row.insert(QStringLiteral("name"), subName);
                const QJsonValue subHeaders =
                    sub.value(QStringLiteral("headers"));
                if (subHeaders.isObject()) {
                    const QVariantMap hm = stringMap(subHeaders.toObject());
                    if (!hm.isEmpty())
                        row.insert(QStringLiteral("headers"), hm);
                }
                list.append(row);
            }
            if (!list.isEmpty()) r.subtitles = list;
        }
        if (!r.url.isEmpty()) out.append(r);
    }
    return out;
}

QVariantMap pluginResultToMap(const PluginStreamResult& r)
{
    QVariantMap out;
    out.insert(QStringLiteral("title"), r.title);
    out.insert(QStringLiteral("name"), r.name);
    out.insert(QStringLiteral("url"), r.url);
    out.insert(QStringLiteral("quality"), r.quality);
    out.insert(QStringLiteral("size"), r.size);
    out.insert(QStringLiteral("language"), r.language);
    out.insert(QStringLiteral("provider"), r.provider);
    out.insert(QStringLiteral("type"), r.type);
    out.insert(QStringLiteral("seeders"), r.seeders);
    out.insert(QStringLiteral("peers"), r.peers);
    out.insert(QStringLiteral("infoHash"), r.infoHash);
    out.insert(QStringLiteral("headers"), r.headers);
    out.insert(QStringLiteral("subtitles"), r.subtitles);
    return out;
}

struct PluginRuntime::Job {
    enum class Kind { Execute, SettingsLayout };
    Kind kind = Kind::Execute;
    QString code;
    QString tmdbId;
    QString mediaType;
    int season = -1;
    int episode = -1;
    QString scraperId;
    QString settingsJson;
    ResultsCallback onResults;
    LayoutCallback onLayout;
};

PluginRuntime::PluginRuntime(QObject* parent) : QObject(parent)
{
    m_actor = new QObject;
    m_actor->moveToThread(&m_worker);
    connect(&m_worker, &QThread::finished, m_actor, &QObject::deleteLater);
    m_worker.start();
}

PluginRuntime::~PluginRuntime()
{
    m_worker.quit();
    m_worker.wait();
}

void PluginRuntime::runJob(Job* job)
{
    const std::unique_ptr<Job> owned(job);
    PluginHost host;
    QString error;
    if (owned->kind == Job::Kind::Execute) {
        QString captured;
        bool done = false;
        const bool ok =
            host.setup(owned->scraperId, owned->settingsJson,
                       [&](const QString& json) {
                           captured = json;
                           done = true;
                       },
                       {}, &error) &&
            host.engine()->eval(wrappedCode(owned->code),
                                QStringLiteral("scraper.js"), &error) &&
            host.engine()->eval(
                getStreamsCall(owned->tmdbId, owned->mediaType, owned->season,
                               owned->episode),
                QStringLiteral("call.js"), &error) &&
            host.pumpUntil([&] { return done; }, kPluginTimeoutMs, &error);
        const QList<PluginStreamResult> rows =
            ok ? parsePluginResults(captured) : QList<PluginStreamResult>{};
        const QString message = ok ? QString() : error;
        QMetaObject::invokeMethod(
            this,
            [cb = std::move(owned->onResults), rows, message] {
                cb(rows, message);
            },
            Qt::QueuedConnection);
        return;
    }
    QString captured = QStringLiteral("[]");
    bool done = false;
    if (host.setup(owned->scraperId, QStringLiteral("{}"), {},
                   [&](const QString& json) {
                       captured = json;
                       done = true;
                   },
                   &error) &&
        host.engine()->eval(wrappedCode(owned->code),
                            QStringLiteral("scraper.js"), &error) &&
        host.engine()->eval(onSettingsCall(), QStringLiteral("call.js"),
                            &error) &&
        host.pumpUntil([&] { return done; }, kPluginTimeoutMs, &error)) {
        // captured holds the layout JSON.
    } else {
        captured = QStringLiteral("[]");
    }
    QMetaObject::invokeMethod(
        this,
        [cb = std::move(owned->onLayout), captured] { cb(captured); },
        Qt::QueuedConnection);
}

void PluginRuntime::execute(const QString& code, const QString& tmdbId,
                            const QString& mediaType, int season, int episode,
                            const QString& scraperId,
                            const QString& settingsJson, ResultsCallback done)
{
    auto* job = new Job;
    job->kind = Job::Kind::Execute;
    job->code = code;
    job->tmdbId = tmdbId;
    job->mediaType = normalizePluginType(mediaType);
    job->season = season;
    job->episode = episode;
    job->scraperId = scraperId;
    job->settingsJson = settingsJson;
    job->onResults = std::move(done);
    QMetaObject::invokeMethod(
        m_actor, [this, job] { runJob(job); }, Qt::QueuedConnection);
}

void PluginRuntime::settingsLayout(const QString& code,
                                   const QString& scraperId,
                                   LayoutCallback done)
{
    auto* job = new Job;
    job->kind = Job::Kind::SettingsLayout;
    job->code = code;
    job->scraperId = scraperId;
    job->onLayout = std::move(done);
    QMetaObject::invokeMethod(
        m_actor, [this, job] { runJob(job); }, Qt::QueuedConnection);
}

QList<PluginStreamResult> PluginRuntime::executeSync(
    const QString& code, const QString& tmdbId, const QString& mediaType,
    int season, int episode, const QString& scraperId,
    const QString& settingsJson, QString* errorOut, int timeoutMs)
{
    PluginHost host;
    QString captured;
    bool done = false;
    QString error;
    if (!host.setup(scraperId, settingsJson,
                    [&](const QString& json) {
                        captured = json;
                        done = true;
                    },
                    {}, &error)) {
        if (errorOut) *errorOut = error;
        return {};
    }
    if (!host.engine()->eval(wrappedCode(code), QStringLiteral("scraper.js"),
                             &error) ||
        !host.engine()->eval(
            getStreamsCall(tmdbId, normalizePluginType(mediaType), season,
                           episode),
            QStringLiteral("call.js"), &error)) {
        if (errorOut) *errorOut = error;
        return {};
    }
    QString pumpError;
    if (!host.pumpUntil([&] { return done; }, timeoutMs, &pumpError)) {
        if (errorOut) *errorOut = pumpError;
        return {};
    }
    return parsePluginResults(captured);
}

} // namespace nuvio::plugins
