#include "nuvio/trailer/TrailerResolver.h"

#include "nuvio/trailer/TrailerKernel.h"

#include <QEventLoop>
#include <cstring>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <cstdio>

namespace nuvio::trailer {

namespace {
constexpr int kRequestTimeoutMs = 20000;   // TRAILER_REQUEST_TIMEOUT_MS parity
} // namespace

TrailerResolver::TrailerResolver(QObject* parent) : QObject(parent) {}

void TrailerResolver::resolveForKey(const QString& keyOrUrl)
{
    const QString videoId = extractVideoId(keyOrUrl);
    if (videoId.isEmpty()) {
        emit trailerFailed(QStringLiteral("invalid YouTube url"));
        return;
    }

    StreamingBuckets merged;
    const auto& chain = clients();
    QNetworkAccessManager nam;

    for (const auto& client : chain) {
        QNetworkRequest req{QUrl(QStringLiteral(
            "https://www.youtube.com/youtubei/v1/player?key=")
            + fallbackInnertubeApiKey())};
        req.setTransferTimeout(kRequestTimeoutMs);
        for (const auto& [name, value] :
             playerRequestHeaders(client, /*visitorData=*/{}))
            req.setRawHeader(name.toUtf8(), value.toUtf8());

        QNetworkReply* rep =
            nam.post(req, playerRequestBody(client, videoId));

        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(rep, &QNetworkReply::finished, &loop,
                         &QEventLoop::quit);
        QObject::connect(&watchdog, &QTimer::timeout, &loop,
                         &QEventLoop::quit);
        watchdog.start(kRequestTimeoutMs);
        loop.exec();
        rep->deleteLater();

        const int status = rep->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (rep->error() != QNetworkReply::NoError
            || status < 200 || status >= 300) {
            std::fprintf(stderr,
                         "trailer: blocked stage=client client=%s "
                         "status=%d\n",
                         client.key, status);
            continue;
        }

        const StreamingBuckets part = parseStreamingData(rep->readAll());
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

    if (!source.has_value()) {
        emit trailerFailed(QStringLiteral("no reachable streams"));
        return;
    }
    emit trailerResolved(source->videoUrl, source->audioUrl);
}

} // namespace nuvio::trailer