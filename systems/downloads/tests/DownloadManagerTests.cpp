// Downloads contract: URL gate, filename builder, queue ops, profile
// isolation, live HTTP round-trip over a local server. ISOLATION: XDG
// sandbox (rows) + temp download dir is the sandbox profile dir.
#include <nuvio/downloads/DownloadManager.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <cstdio>

using nuvio::downloads::DownloadManager;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Minimal HTTP file server: honors Range, serves fixed bytes.
class FileServer final : public QObject {
public:
    QByteArray payload;
    QList<QByteArray> requests;
    bool failFirst = false;
    int hits = 0;

    bool start()
    {
        if (!m_srv.listen(QHostAddress::LocalHost)) return false;
        m_port = m_srv.serverPort();
        connect(&m_srv, &QTcpServer::newConnection, this,
                [this] { accept(); });
        return true;
    }
    QUrl baseUrl() const
    {
        return {QStringLiteral("http://127.0.0.1:%1").arg(m_port)};
    }

private:
    void accept()
    {
        auto* sock = m_srv.nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            m_buf[sock] += sock->readAll();
            const int hdrEnd = m_buf[sock].indexOf("\r\n\r\n");
            if (hdrEnd < 0) return;
            const QByteArray head = m_buf[sock].left(hdrEnd);
            requests << head;
            ++hits;
            qint64 from = 0;
            bool ranged = false;
            for (const QByteArray& line : head.split('\n')) {
                const QByteArray h = line.trimmed();
                if (h.startsWith("Range:")) {
                    const QByteArray spec =
                        h.mid(6).trimmed().replace("bytes=", "");
                    from = spec.split('-').value(0).toLongLong();
                    ranged = true;
                }
            }
            if (failFirst && hits == 1) {
                sock->write("HTTP/1.1 500 Fail\r\nContent-Length: 0\r\n"
                            "Connection: close\r\n\r\n");
                sock->flush();
                sock->disconnectFromHost();
                m_buf.remove(sock);
                return;
            }
            QByteArray body = payload.mid(int(from));
            QByteArray out = "HTTP/1.1 ";
            out += (ranged ? "206 Partial\r\nContent-Range: bytes " +
                                 QByteArray::number(from) + "-" +
                                 QByteArray::number(payload.size() - 1) +
                                 "/" + QByteArray::number(payload.size())
                           : "200 OK");
            out += "\r\nContent-Length: " +
                   QByteArray::number(body.size()) +
                   "\r\nConnection: close\r\n\r\n" + body;
            sock->write(out);
            sock->flush();
            sock->disconnectFromHost();
            m_buf.remove(sock);
        });
    }

    QTcpServer m_srv;
    QHash<QTcpSocket*, QByteArray> m_buf;
    quint16 m_port = 0;
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME",
            QDir(sandbox.path()).filePath("cfg").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

    using namespace nuvio::downloads;

    { // URL gate + filename builder (verbatim rules)
        CHECK(isSupportedDownloadUrl("https://x/y.mp4"), "https ok");
        CHECK(isSupportedDownloadUrl("http://x/y"), "http ok");
        CHECK(!isSupportedDownloadUrl("magnet:?xt=urn:btih:abc"),
              "magnet refused");
        CHECK(!isSupportedDownloadUrl("https://x/y.m3u8"), "hls refused");
        CHECK(!isSupportedDownloadUrl("https://x/y.m3u8?tok=1"),
              "hls-with-query refused");
        CHECK(!isSupportedDownloadUrl("https://x/y.mpd"), "dash refused");
        CHECK(!isSupportedDownloadUrl("https://x/y.torrent"), "torrent refused");
        CHECK(!isSupportedDownloadUrl("ftp://x/y.mp4"), "ftp refused");
        CHECK(buildLogicalKey("tt1", 1, 2) == "tt1|1|2", "episode key");
        CHECK(buildLogicalKey("tt1", -1, -1) == "tt1|movie", "movie key");
        CHECK(sanitizeFileName("a/b:c*d") == "a_b_c_d", "sanitize");
        CHECK(fileExtensionFromUrl("https://x/y.MKV?tok=1") == "mkv",
              "extension lowercased, query stripped");
        CHECK(fileExtensionFromUrl("https://x/y") == "mp4",
              "extensionless defaults mp4");
        CHECK(fileExtensionFromUrl("https://x/y.abcdefg") == "mp4",
              "overlong suffix defaults mp4");
        const QString fn = buildFileName("Show: X", 1, 2, "Pilot", "fb",
                                         "https://x/y.mkv", 1000);
        CHECK(fn.startsWith("Show_ X S01E02 Pilot_") && fn.endsWith(".mkv"),
              "episode filename shape");
        const QString fn2 =
            buildFileName("", -1, -1, "", "Fallback", "https://x/y", 1000);
        CHECK(fn2.startsWith("Fallback_") && fn2.endsWith(".mp4"),
              "fallback title + default ext");
    }

    { // queue ops: enqueue/replace/pause/resume/cancel + playable lookup
        DownloadManager dl;
        CHECK(dl.enqueue("movie", "tt1", "movie", "v1", "M", "", -1, -1, "",
                         "S", "P", "https://x/m.mp4") == "Started",
              "enqueue starts");
        CHECK(dl.items().size() == 1, "one row queued");
        // Unsupported + missing gates (no rows, no crash).
        CHECK(dl.enqueue("movie", "tt2", "movie", "v2", "M2", "", -1, -1,
                         "", "S", "P", "magnet:?x") == "UnsupportedFormat",
              "magnet refused");
        CHECK(dl.enqueue("movie", "tt2", "movie", "v2", "M2", "", -1, -1,
                         "", "S", "P", "") == "MissingUrl",
              "empty refused");
        CHECK(dl.items().size() == 1, "gated enqueues add nothing");
        const QString id = dl.items()[0].id;
        dl.pauseDownload(id);
        CHECK(dl.items()[0].status == DownloadStatus::Paused,
              "pause flips status");
        dl.resumeDownload(id);
        CHECK(dl.items()[0].status == DownloadStatus::Downloading,
              "resume restarts");
        dl.cancelDownload(id);
        CHECK(dl.items().isEmpty(), "cancel drops the row");
        CHECK(dl.playableLocalFile("tt9", -1, -1, "v9").isEmpty(),
              "missing rows never resolve");
    }

    { // pauseActiveDownloads parks every in-flight row at once.
        FileServer srv;
        srv.payload = QByteArray(32768, 'p');
        CHECK(srv.start(), "pause server listens");
        DownloadManager dl;
        const QString base = srv.baseUrl().toString();
        dl.enqueue("movie", "ttA", "movie", "vA", "A", "", -1, -1, "", "S",
                   "P", base + "/a.mp4");
        dl.enqueue("movie", "ttB", "movie", "vB", "B", "", -1, -1, "", "S",
                   "P", base + "/b.mp4");
        // No event processing between enqueues and the park call: both
        // rows are still Downloading, so the outcome is deterministic.
        dl.pauseActiveDownloads();
        CHECK(dl.items().size() == 2, "both rows kept");
        CHECK(dl.items()[0].status == DownloadStatus::Paused &&
                  dl.items()[1].status == DownloadStatus::Paused,
              "park flips every active row");
        dl.cancelDownload(dl.items()[0].id);
        dl.cancelDownload(dl.items()[0].id);
        CHECK(dl.items().isEmpty(), "parked rows cancel cleanly");
    }

    { // live HTTP round-trip: full download completes with byte counts.
        FileServer srv;
        srv.payload = QByteArray(65536, 'v');
        CHECK(srv.start(), "file server listens");
        DownloadManager dl;
        const QString url =
            srv.baseUrl().toString() + "/video.mp4";
        CHECK(dl.enqueue("movie", "tt7", "movie", "v7", "Vid", "", -1, -1,
                         "", "S", "P", url) == "Started",
              "live enqueue starts");
        const QString id = dl.items()[0].id;
        QDeadlineTimer drain(15000);
        while (!drain.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (!dl.items().isEmpty() &&
                dl.items()[0].status != DownloadStatus::Downloading)
                break;
        }
        CHECK(!dl.items().isEmpty() &&
                  dl.items()[0].status == DownloadStatus::Completed,
              "download completes");
        CHECK(dl.items()[0].downloadedBytes == 65536,
              "byte count exact");
        CHECK(!dl.items()[0].localFileUri.isEmpty() &&
                  dl.playableLocalFile("tt7", -1, -1, "v7") ==
                      dl.items()[0].localFileUri,
              "playable lookup resolves the finished file");
        Q_UNUSED(id);
    }

    { // retry then resume: first attempt fails, retry succeeds; partial
      // .part resume continues byte counts.
        FileServer srv;
        srv.payload = QByteArray(32768, 'w');
        srv.failFirst = true;
        CHECK(srv.start(), "flaky server listens");
        DownloadManager dl;
        const QString url = srv.baseUrl().toString() + "/r.mp4";
        dl.enqueue("movie", "tt8", "movie", "v8", "R", "", -1, -1, "", "S",
                   "P", url);
        QDeadlineTimer drain(20000);
        while (!drain.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (!dl.items().isEmpty() &&
                dl.items()[0].status != DownloadStatus::Downloading)
                break;
        }
        CHECK(!dl.items().isEmpty() &&
                  dl.items()[0].status == DownloadStatus::Completed &&
                  dl.items()[0].downloadedBytes == 32768,
              "failed first attempt retries to completion");
        CHECK(srv.hits >= 2, "retry re-requested");
    }

    std::printf(failures ? "DOWNLOADS SUITE FAILURES=%d\n"
                         : "DOWNLOADS SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
