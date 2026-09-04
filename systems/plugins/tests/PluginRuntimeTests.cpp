// Offline contract for plugin execution: result parsing plus
// end-to-end scraper runs (fetch + cheerio + crypto shapes through a
// local stub). No external network.
#include <nuvio/plugins/PluginRuntime.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::plugins::normalizePluginType;
using nuvio::plugins::parsePluginResults;
using nuvio::plugins::PluginRuntime;

namespace {

class ScraperStub final : public QObject {
public:
    bool start()
    {
        connect(&m_srv, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* sock = m_srv.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                m_buf[sock] += sock->readAll();
                const int split = m_buf[sock].indexOf("\r\n\r\n");
                if (split < 0) return;
                int contentLength = 0;
                const QRegularExpression cl(
                    QStringLiteral("Content-Length:\\s*(\\d+)"));
                if (const auto m = cl.match(QString::fromUtf8(
                        m_buf[sock].left(split)));
                    m.hasMatch())
                    contentLength = m.captured(1).toInt();
                if (m_buf[sock].size() < split + 4 + contentLength) return;
                m_buf.remove(sock);
                const QByteArray body =
                    "<html><body><div class=\"res\">"
                    "<a href=\"https://cdn.example/ep1.mkv\" "
                    "data-q=\"1080p\">Episode 1</a>"
                    "<a href=\"https://cdn.example/ep2.mkv\">Episode 2</a>"
                    "</div></body></html>";
                const QByteArray out =
                    QByteArray("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                               "Content-Length: ") +
                    QByteArray::number(body.size()) +
                    QByteArray("\r\nConnection: close\r\n\r\n") + body;
                sock->write(out);
                sock->flush();
                sock->disconnectFromHost();
            });
        });
        if (!m_srv.listen(QHostAddress::LocalHost, 0)) return false;
        m_port = m_srv.serverPort();
        return true;
    }

    [[nodiscard]] QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
    }

private:
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

    { // parsing rules (fork parseJsonResults parity)
        CHECK(normalizePluginType("series") == "tv", "series folds");
        CHECK(normalizePluginType("SHOW") == "tv", "show folds");
        CHECK(normalizePluginType("movie") == "movie", "movie kept");
        CHECK(parsePluginResults("garbage").isEmpty(), "garbage empty");
        CHECK(parsePluginResults("{\"a\":1}").isEmpty(), "object empty");
        const auto rows = parsePluginResults(
            "[{\"title\":\"T\",\"url\":\"https://x/y.mkv\",\"quality\":"
            "\"1080p\",\"seeders\":5,\"headers\":{\"X-A\":\"b\"},"
            "\"subtitles\":[{\"url\":\"https://x/s.srt\",\"language\":"
            "\"en\",\"name\":\"EN\"}]},"
            "{\"name\":\"N\",\"url\":{\"url\":\"https://x/z.mkv\"}},"
            "{\"title\":\"NoUrl\"},"
            "{\"title\":\"[object Object]\",\"url\":\"https://x/w.mkv\"}]");
        CHECK(rows.size() == 3, "url required, object-url accepted");
        CHECK(rows[0].title == "T" && rows[0].quality == "1080p" &&
                  rows[0].seeders == 5 && rows[0].peers == -1,
              "fields + seeders, peers default");
        CHECK(rows[0].headers.value("X-A").toString() == "b",
              "headers map");
        CHECK(rows[0].subtitles.size() == 1 &&
                  rows[0]
                          .subtitles[0]
                          .toMap()
                          .value("language")
                          .toString() == "en",
              "subtitles shape");
        CHECK(rows[1].title == "N", "name falls back to title");
        CHECK(rows[2].title == "Unknown", "[object guard + default");
    }

    { // end-to-end scraper: fetch + cheerio + settings + module shape
        ScraperStub stub;
        CHECK(stub.start(), "scraper stub listens");
        PluginRuntime runtime;
        const QString code =
            QStringLiteral(
                "module.exports.getStreams = async function(tmdbId, "
                "mediaType, season, episode) {\n"
                "  if (tmdbId !== '550' || mediaType !== 'movie') throw new "
                "Error('bad args:' + tmdbId + '/' + mediaType);\n"
                "  var secret = CryptoJS.SHA256('abc').toString().slice(0, "
                "8);\n"
                "  var res = await fetch('%1/search?q=' + secret);\n"
                "  var html = await res.text();\n"
                "  var $ = require('cheerio').load(html);\n"
                "  var out = [];\n"
                "  $('div.res a').each(function(i, el) {\n"
                "    out.push({ title: el.text() + ' [' + "
                "SCRAPER_SETTINGS.tag + ']',\n"
                "               url: el.attr('href'),\n"
                "               quality: el.attr('data-q') || 'SD',\n"
                "               seeders: 7 });\n"
                "  });\n"
                "  return out;\n"
                "};")
                .arg(stub.baseUrl());
        QString error;
        const auto rows = runtime.executeSync(
            code, "550", "movie", -1, -1, "test-scraper",
            "{\"tag\":\"TAG\"}", &error, 30000);
        if (!error.isEmpty())
            std::fprintf(stderr, "DBG exec err=[%s]\n",
                         error.toUtf8().constData());
        CHECK(error.isEmpty(), "scraper executes cleanly");
        CHECK(rows.size() == 2, "two streams scraped");
        CHECK(rows[0].url == "https://cdn.example/ep1.mkv" &&
                  rows[0].title == "Episode 1 [TAG]" &&
                  rows[0].quality == "1080p" && rows[0].seeders == 7,
              "first row: url/title/settings/quality/seeders");
        CHECK(rows[1].quality == "SD", "missing attr falls back");
    }

    { // failure modes stay honest (empty rows, message set)
        PluginRuntime runtime;
        QString error;
        const auto rows = runtime.executeSync(
            "module.exports.getStreams = async function() { throw new "
            "Error('boom'); };",
            "1", "movie", -1, -1, "bad-scraper", "{}", &error, 15000);
        CHECK(rows.isEmpty(), "throwing scraper yields no rows");
        QString error2;
        const auto rows2 = runtime.executeSync(
            "module.exports.getStreams = async function() { return [{"
            "title:'x'}]; };",
            "1", "movie", -1, -1, "nourl-scraper", "{}", &error2, 15000);
        CHECK(rows2.isEmpty() && error2.isEmpty(),
              "url-less results filter silently");
        // Async entry point delivers on the caller thread.
        bool delivered = false;
        runtime.execute("module.exports.getStreams = async function() { "
                        "return [{title:'A', url:'https://x/a.mkv'}]; };",
                        "1", "movie", -1, -1, "async-scraper", "{}",
                        [&](const auto& r, const QString& e) {
                            delivered = r.size() == 1 && e.isEmpty();
                        });
        QDeadlineTimer drain(15000);
        while (!drain.hasExpired() && !delivered)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(delivered, "async execute delivers to caller thread");
    }

    std::printf(failures ? "RUNTIME SUITE FAILURES=%d\n"
                         : "RUNTIME SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
