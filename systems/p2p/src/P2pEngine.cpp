#include "nuvio/p2p/P2pEngine.h"

#include "nuvio/p2p/TorrServerProcess.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace nuvio::p2p {

namespace {
constexpr int kMetadataDeadlineAttempts = 15;  // 15 x 1s = Compose parity
constexpr int kIdleStopMs               = 60000;
} // namespace

P2pEngine::P2pEngine(TorrServerProcess* binary, QObject* parent)
    : QObject(parent), m_binary(binary), m_api(new QNetworkAccessManager(this))
{
    Q_ASSERT(m_binary);
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, [this] {
        const quint64 gen = m_generation;
        const QString hash = m_activeHash;
        if (hash.isEmpty()) return;
        QByteArray body;
        if (!postJson(QStringLiteral("/torrents"),
                      getTorrentRequestBody(hash), &body)) return;
        const auto stats = parseTorrentStats(body);
        if (stats)
            emit statsUpdated(gen, stats->preloadedBytes,
                              stats->torrentSize, stats->downloadSpeedBps,
                              stats->peers, stats->seeds);
    });
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this,
            [this] { m_binary->stop(); });
}

quint64 P2pEngine::startStream(const QString& infoHash,
                               const QString& filename)
{
    m_idleTimer.stop();                      // an active session cancels idle-stop
    const quint64 gen = ++m_generation;
    m_statsTimer.stop();
    m_activeHash.clear();

    const QString magnet = buildMagnetUri(infoHash);
    if (magnet.isEmpty()) {
        failToken(gen, QStringLiteral("invalid torrent info-hash"));
        return gen;
    }

    // Binary bring-up is shared with failure tracking: a crash or failed
    // start while THIS generation is active fails it (queued on both ends).
    connect(m_binary, &TorrServerProcess::failed, this,
            [this, gen](const QString& reason) {
                if (gen == m_generation) failToken(gen, reason);
            },
            Qt::QueuedConnection);
    connect(m_binary, &TorrServerProcess::ready, this,
            [this, gen, magnet, filename] {
                if (gen == m_generation) beginSession(gen, magnet, filename);
            },
            Qt::QueuedConnection);

    if (!m_binary->isRunning())
        m_binary->start(resolveServerBinaryPath());
    else
        beginSession(gen, magnet, filename);   // already up: straight in
    return gen;
}

void P2pEngine::beginSession(quint64 gen, const QString& magnet,
                             const QString& filename)
{
    QByteArray reply;
    if (!postJson(QStringLiteral("/torrents"),
                  addTorrentRequestBody(magnet), &reply)) {
        failToken(gen, QStringLiteral("Failed to add torrent"));
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(reply).object();
    const QString hash = obj.value(QLatin1String("hash")).toString();
    if (hash.isEmpty() || hash.trimmed().isEmpty()) {
        failToken(gen, QStringLiteral("Failed to add torrent"));
        return;
    }
    if (gen != m_generation) return;           // superseded mid-flight
    m_activeHash = hash;
    awaitMetadata(gen, hash, magnet, filename, kMetadataDeadlineAttempts);
}

void P2pEngine::awaitMetadata(quint64 gen, const QString& hash,
                              const QString& magnet, const QString& filename,
                              int attemptsLeft)
{
    if (gen != m_generation) return;

    QByteArray body;
    QList<TorrentFile> files;
    if (postJson(QStringLiteral("/torrents"), getTorrentRequestBody(hash), &body)) {
        if (const auto stats = parseTorrentStats(body)) files = stats->files;
    }
    if (!files.isEmpty()) {
        const int idx = resolveFileIndex(files, /*requestedIdx=*/ -1, filename);
        if (gen != m_generation) return;
        const QString url = buildStreamUrl(m_binary->baseUrl(), magnet, idx);
        QMetaObject::invokeMethod(this,
            [this, gen, url] { emit streamReady(gen, url); },
            Qt::QueuedConnection);
        m_statsTimer.start();                  // 1 Hz progress from here on
        return;
    }

    if (attemptsLeft <= 0) {
        // Metadata never arrived: Compose-parity honest fallback to id 1.
        if (gen != m_generation) return;
        const QString url = buildStreamUrl(m_binary->baseUrl(), magnet, 1);
        QMetaObject::invokeMethod(this,
            [this, gen, url] { emit streamReady(gen, url); },
            Qt::QueuedConnection);
        m_statsTimer.start();
        return;
    }

    QTimer::singleShot(1000, this, [this, gen, hash, magnet, filename,
                                    attemptsLeft] {
        awaitMetadata(gen, hash, magnet, filename, attemptsLeft - 1);
    });
}

void P2pEngine::stopStream()
{
    ++m_generation;                          // invalidate every in-flight token
    m_statsTimer.stop();
    const QString hash = m_activeHash;
    m_activeHash.clear();
    if (!hash.isEmpty()) {
        postJson(QStringLiteral("/torrents"), dropTorrentRequestBody(hash),
                 nullptr);                   // fire-and-forget
    }
    // Idle binary stop: a finished session must not leave a resident server
    // holding its RAM piece cache (Compose memory-parity rule). Any new
    // startStream cancels this.
    m_idleTimer.start(kIdleStopMs);
}

void P2pEngine::applyCacheSettings(int cacheMb)
{
    // GET-modify-POST: the /settings schema drifts between versions, so we
    // fetch the current object and override ONLY "cache" (MB).
    QNetworkRequest req{QUrl(m_binary->baseUrl() + QStringLiteral("/settings"))};
    req.setTransferTimeout(10000);
    QNetworkReply* rep = m_api->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, cacheMb, rep] {
        rep->deleteLater();
        const int status =
            rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status < 200 || status >= 300) return;
        QByteArray updated;
        if (!mergeCacheSettings(rep->readAll(), cacheMb, &updated)) return;
        QNetworkRequest post{QUrl(m_binary->baseUrl() + QStringLiteral("/settings"))};
        post.setTransferTimeout(30000);
        post.setHeader(QNetworkRequest::ContentTypeHeader,
                       QStringLiteral("application/json"));
        QNetworkReply* preply = m_api->post(post, updated);
        connect(preply, &QNetworkReply::finished, preply,
                &QObject::deleteLater);
    });
}

bool P2pEngine::postJson(const QString& path, const QByteArray& body,
                         QByteArray* responseOut)
{
    // Synchronous-by-intent helper on the UI thread would be wrong; this is
    // called ONLY from engine-internal continuations where blocking one
    // short local request is unacceptable. So: spin an event loop bounded by
    // a 30 s timeout (same wall as Compose's 30 s request timeout).
    QNetworkRequest req{QUrl(m_binary->baseUrl() + path)};
    req.setTransferTimeout(30000);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    QNetworkReply* rep = m_api->post(req, body);

    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(30000);
    loop.exec();                             // nested but strictly time-boxed
    rep->deleteLater();

    const int status =
        rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = rep->error() == QNetworkReply::NoError
                 && status >= 200 && status < 300;
    if (ok && responseOut) *responseOut = rep->readAll();
    return ok;
}

void P2pEngine::failToken(quint64 gen, const QString& reason)
{
    if (gen != m_generation) return;         // stale failure for old start
    m_statsTimer.stop();
    QMetaObject::invokeMethod(this,
        [this, gen, reason] { emit streamFailed(gen, reason); },
        Qt::QueuedConnection);
}

} // namespace nuvio::p2p
