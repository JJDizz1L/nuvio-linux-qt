#include "nuvio/trailer/TrailerResolver.h"

#include "nuvio/trailer/TrailerKernel.h"

#include <QEventLoop>
#include <cstring>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <cstdio>
#include <thread>

namespace nuvio::trailer {

namespace {
constexpr int kRequestTimeoutMs = 20000;   // TRAILER_REQUEST_TIMEOUT_MS parity
constexpr int kProbeTimeoutMs  = 2000;     // PROBE client connect/read parity

/// Shared one-shot request helper: POSTs/GETs, drains until finished or
/// watchdog, returns the reply for the caller to inspect + deleteLater.
QNetworkReply* blockingRequest(QNetworkAccessManager& nam,
                               QNetworkRequest req,
                               const QByteArray* postBody, int timeoutMs)
{
    req.setTransferTimeout(timeoutMs);
    QNetworkReply* rep =
        postBody ? nam.post(req, *postBody) : nam.get(req);
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(rep, &QNetworkReply::finished, &loop,
                     &QEventLoop::quit);
    QObject::connect(&watchdog, &QTimer::timeout, &loop,
                     &QEventLoop::quit);
    watchdog.start(timeoutMs);
    loop.exec();
    return rep;
}
} // namespace

TrailerResolver::TrailerResolver(QObject* parent) : QObject(parent) {}

QString TrailerResolver::fetchVisitorData(QNetworkAccessManager& nam,
                                          const QString& videoId)
{
    // One ANDROID player-API call harvests the session visitor token from
    // responseContext (Compose fetchWatchConfig parity; watch-page HTML is
    // 429-gated for non-browser clients). Pure fetcher: caching happens in the
    // worker. Non-fatal - the fallback API key path works without a token.
    const YouTubeClient* android = nullptr;
    for (const auto& c : clients())
        if (std::strcmp(c.key, "android") == 0) { android = &c; break; }
    if (!android) return {};

    QNetworkRequest req{QUrl(QStringLiteral(
        "https://www.youtube.com/youtubei/v1/player?key=")
        + fallbackInnertubeApiKey())};
    for (const auto& [name, value] : playerRequestHeaders(*android))
        req.setRawHeader(name.toUtf8(), value.toUtf8());

    const QByteArray body = playerRequestBody(*android, videoId);
    QNetworkReply* rep = blockingRequest(nam, req, &body, kRequestTimeoutMs);
    const int status = rep->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray resp =
        (rep->error() == QNetworkReply::NoError
         && status >= 200 && status < 300) ? rep->readAll() : QByteArray();
    rep->deleteLater();

    QString visitor;
    if (!resp.isEmpty()) {
        visitor = QJsonDocument::fromJson(resp).object()
                      .value(QLatin1String("responseContext")).toObject()
                      .value(QLatin1String("visitorData")).toString();
    }
    std::fprintf(stderr, "trailer: visitor-data android %s\n",
                 visitor.isEmpty() ? "missing" : "ok");
    return visitor;
}

bool TrailerResolver::isUrlReachable(QNetworkAccessManager& nam,
                                     const QString& url)
{
    // Compose isUrlReachable parity: Range-probe the head (and tail) of the
    // object; a 206 proves the host serves it. Without clen, a 2xx on the
    // full-range request is used.
    const QUrl parsed(url);
    const qint64 clen =
        (parsed.isValid() && parsed.hasQuery())
            ? QUrlQuery(parsed).queryItemValue(QLatin1String("clen"))
                  .toLongLong()
            : 0;

    QVector<QPair<qint64, qint64>> ranges;
    if (clen > 0) {
        ranges.append({0, qMin<qint64>(65535, clen - 1)});
        ranges.append({qMax<qint64>(0, clen - 65536), clen - 1});
        if (ranges.size() == 2 && ranges.first() == ranges.last())
            ranges.removeLast();
    } else {
        ranges.append({0, 0});
    }

    for (const auto& ra : ranges) {
        QNetworkRequest req{QUrl(url)};
        req.setTransferTimeout(kProbeTimeoutMs);
        req.setRawHeader(
            QByteArrayLiteral("Range"),
            QByteArrayLiteral("bytes=")
                + QByteArray::number(ra.first) + QByteArrayLiteral("-")
                + QByteArray::number(ra.second));

        QNetworkReply* rep = blockingRequest(nam, req, nullptr,
                                             kProbeTimeoutMs);
        const bool ok = rep->error() == QNetworkReply::NoError
            && (rep->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt() == 206
                || (clen == 0 && ra.first == 0
                    && rep->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                           .toInt() >= 200
                    && rep->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                           .toInt() < 300));
        rep->deleteLater();
        if (!ok) return false;
    }
    return true;
}

QString TrailerResolver::resolveReachableUrlOrNull(
    QNetworkAccessManager& nam, const QString& url)
{
    const QStringList candidates = hostRotationCandidates(url);
    if (candidates.isEmpty()) return {};
    if (candidates.size() == 1) {
        return isUrlReachable(nam, candidates.first())
                   ? candidates.first() : QString{};
    }
    for (const QString& c : candidates) {
        if (isUrlReachable(nam, c)) {
            std::fprintf(stderr, "trailer: probe ok candidates=%lld "
                         "selectedHost=%s\n",
                         static_cast<long long>(candidates.size()),
                         QUrl(c).host().toUtf8().constData());
            return c;
        }
    }
    return {};
}


void TrailerResolver::resolveForKey(const QString& keyOrUrl)
{
    m_ambient = false;
    beginResolve(keyOrUrl);
}

void TrailerResolver::resolveForKeyAmbient(const QString& keyOrUrl)
{
    m_ambient = true;
    beginResolve(keyOrUrl);
}

void TrailerResolver::beginResolve(const QString& keyOrUrl)
{
    const QString videoId = extractVideoId(keyOrUrl);
    if (videoId.isEmpty()) {
        if (m_ambient) emit ambientFailed(QStringLiteral("invalid YouTube url"));
        else           emit trailerFailed(QStringLiteral("invalid YouTube url"));
        return;
    }
    if (m_resolving) return;             // one in-flight resolution only
    m_resolving = true;
    emit resolvingChanged();
    std::thread([this, videoId] { runResolveWorker(videoId); }).detach();
}

void TrailerResolver::runResolveWorker(const QString& videoId)
{
    QNetworkAccessManager nam;           // worker-thread NAM

    // Visitor token: session cache (worker-written + main-read via the
    // in-flight single-worker gate), otherwise one ANDROID fetch.
    QString visitor;
    {
        std::lock_guard<std::mutex> g(m_cacheMutex);
        visitor = m_visitorData;
    }
    if (visitor.isEmpty()) {
        visitor = fetchVisitorData(nam, videoId);
        if (!visitor.isEmpty()) {
            std::lock_guard<std::mutex> g(m_cacheMutex);
            m_visitorData = visitor;
        }
    }

    StreamingBuckets merged;
    const auto& chain = clients();

    for (const auto& client : chain) {
        QNetworkRequest req{QUrl(QStringLiteral(
            "https://www.youtube.com/youtubei/v1/player?key=")
            + fallbackInnertubeApiKey())};
        for (const auto& [name, value] :
             playerRequestHeaders(client, visitor))
            req.setRawHeader(name.toUtf8(), value.toUtf8());

        const QByteArray body = playerRequestBody(client, videoId, visitor);
        QNetworkReply* rep = blockingRequest(nam, req, &body,
                                             kRequestTimeoutMs);

        const int status = rep->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray resp =
            (rep->error() == QNetworkReply::NoError
             && status >= 200 && status < 300)
                ? rep->readAll() : QByteArray();
        rep->deleteLater();
        if (resp.isEmpty()) {
            std::fprintf(stderr,
                         "trailer: blocked stage=client client=%s "
                         "status=%d\n",
                         client.key, status);
            continue;
        }

        const StreamingBuckets part = parseStreamingData(resp);
        // Priority rides the CLIENT ROW (chain order), preserving Compose's
        // tie-break contract inside each bucket.
        int priority = 0;
        for (const auto& c : chain) {
            if (std::strcmp(c.key, client.key) == 0) break;
            ++priority;
        }
        for (auto c : part.progressive) {
            c.client = QLatin1String(client.key); c.priority = priority;
            merged.progressive.append(c);
        }
        for (auto c : part.video) {
            c.client = QLatin1String(client.key); c.priority = priority;
            merged.video.append(c);
        }
        for (auto c : part.audio) {
            c.client = QLatin1String(client.key); c.priority = priority;
            merged.audio.append(c);
        }
        if (merged.hlsManifestUrl.isEmpty())
            merged.hlsManifestUrl = part.hlsManifestUrl;
    }

    const auto source = buildPlaybackSource(
        orderSeparate(merged.progressive), orderSeparate(merged.video),
        orderSeparate(merged.audio), merged.hlsManifestUrl);

    bool ok = source.has_value();
    QString videoUrl, audioUrl, reason;
    if (ok) {
        // Host-rotation probe the chosen URLs (slice 3): a googlevideo
        // primary that won't serve is swapped for an `mn` alternate.
        videoUrl = resolveReachableUrlOrNull(nam, source->videoUrl);
        if (videoUrl.isEmpty()) {
            ok = false;
            reason = QStringLiteral("no reachable streams");
        } else {
            audioUrl = source->audioUrl;
            if (!audioUrl.isEmpty())
                audioUrl = resolveReachableUrlOrNull(nam, audioUrl);
        }
    } else {
        reason = QStringLiteral("no reachable streams");
    }

    // Marshal the terminal signal (and resolving=false) back on the QML
    // thread. Everything captured here is a by-value copy, safe from the
    // worker's teardown.
    QMetaObject::invokeMethod(
        this,
        [this, ok, videoUrl, audioUrl, reason] {
            if (ok) {
                if (m_ambient) emit ambientResolved(videoUrl, audioUrl);
                else           emit trailerResolved(videoUrl, audioUrl);
            } else {
                if (m_ambient) emit ambientFailed(reason);
                else           emit trailerFailed(reason);
            }
            m_resolving = false;
            emit resolvingChanged();
        },
        Qt::QueuedConnection);
}

} // namespace nuvio::trailer

