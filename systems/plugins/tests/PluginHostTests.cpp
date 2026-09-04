// Offline contract for the plugin host: polyfill load, sync JS
// builtins, crypto bridges, cheerio, and async fetch through the full
// polyfill fetch() against a local stub. No external network.
#include <nuvio/plugins/PluginHost.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::plugins::PluginHost;

namespace {

class FetchStub final : public QObject {
public:
    int hits = 0;
    QString lastMethod;
    QString lastPath;

    bool start()
    {
        connect(&m_srv, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* sock = m_srv.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                m_buf[sock] += sock->readAll();
                const int split = m_buf[sock].indexOf("\r\n\r\n");
                if (split < 0) return;
                // Wait for the full body (early close can reset
                // in-flight uploads; suite-proven pattern).
                int contentLength = 0;
                const QRegularExpression cl(
                    QStringLiteral("Content-Length:\\s*(\\d+)"));
                if (const auto m = cl.match(QString::fromUtf8(
                        m_buf[sock].left(split)));
                    m.hasMatch())
                    contentLength = m.captured(1).toInt();
                if (m_buf[sock].size() < split + 4 + contentLength) return;
                const QByteArray raw = m_buf[sock];
                m_buf.remove(sock);
                ++hits;
                const QString head =
                    QString::fromUtf8(raw.left(split)).section("\r\n", 0, 0);
                lastMethod = head.section(' ', 0, 0);
                lastPath =
                    head.section(' ', 1, 1).section('?', 0, 0);
                const QByteArray body =
                    "{\"streams\":[{\"url\":\"https://cdn.example/a.mkv\"}]}";
                const QByteArray out =
                    QByteArray("HTTP/1.1 200 OK\r\nContent-Type: "
                               "application/json\r\nX-Marker: hi\r\nContent-Length: ") +
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

    { // polyfill load + sync builtins (no natives touched)
        PluginHost host;
        QStringList logs;
        host.setLogger([&](const QString& line) { logs.append(line); });
        QString err;
        CHECK(host.setup("scraper-1", "{\"quality\":\"1080p\"}", {},
                         {},
                         &err),
              "polyfill loads");
        if (!err.isEmpty())
            std::fprintf(stderr, "DBG polyfill err=[%s]\n",
                         err.toUtf8().constData());
        CHECK(host.engine()->globalToJson("SCRAPER_ID") == "\"scraper-1\"",
              "scraper id lands");
        CHECK(host.engine()->globalToJson("SCRAPER_SETTINGS") ==
                  "{\"quality\":\"1080p\"}",
              "settings land");
        QString out;
        CHECK(host.engine()->evalToString("atob('aGk=')", "t", &out) &&
                  out == "\"hi\"",
              "atob works");
        CHECK(host.engine()->evalToString("btoa('hi')", "t", &out) &&
                  out == "\"aGk=\"",
              "btoa works");
        CHECK(host.engine()->evalToString(
                  "var u = new URL('https://ex.example:8080/p?q=1#frag');"
                  "u.hostname + '|' + u.port + '|' + u.pathname + '|' + "
                  "u.search + '|' + u.hash",
                  "t", &out) &&
                  out == "\"ex.example|8080|/p|?q=1|#frag\"",
              "URL parses through the native bridge");
        CHECK(host.engine()->evalToString(
                  "[1,[2,[3]]].flat(2).join(',')", "t", &out) &&
                  out == "\"1,2,3\"",
              "array flat polyfill");
        CHECK(host.engine()->evalToString(
                  "var te = new TextEncoder().encode('Hi');"
                  "te.length + ':' + te[0]",
                  "t", &out) &&
                  out == "\"2:72\"",
              "TextEncoder via utf8 bridge");
        CHECK(host.engine()->evalToString(
                  "var CryptoJS2 = require('crypto-js');"
                  "typeof CryptoJS2.SHA256",
                  "t", &out) &&
                  out == "\"function\"",
              "require crypto-js");
        host.engine()->eval("console.log('hello', 42, true);", "t");
        CHECK(!logs.isEmpty() && logs.last().contains("hello") &&
                  logs.last().contains("42"),
              "console captured with js number formatting");
    }

    { // crypto bridges end-to-end through CryptoJS + subtle
        PluginHost host;
        QString err;
        CHECK(host.setup("s", "{}", {}, {}, &err), "host for crypto");
        QString out;
        CHECK(host.engine()->evalToString(
                  "CryptoJS.SHA256('abc').toString()", "t", &out) &&
                  out == "\"ba7816bf8f01cfea414140de5dae2223b00361a396"
                         "177a9cb410ff61f20015ad\"",
              "CryptoJS SHA256 via native digest");
        CHECK(host.engine()->evalToString(
                  "CryptoJS.enc.Hex.stringify(CryptoJS.MD5('abc'))", "t",
                  &out) &&
                  out == "\"900150983cd24fb0d6963f7d28e17f72\"",
              "CryptoJS MD5");
        // WebCrypto subtle.digest is async: pump the job queue.
        CHECK(host.engine()->eval(
                  "var subtleOut = 'pending'; crypto.subtle.digest("
                  "'SHA-256', new TextEncoder().encode('abc')).then("
                  "function(buf) { var b = new Uint8Array(buf); var h = '';"
                  "for (var i = 0; i < b.length; i++) h += b[i].toString(16)"
                  ".padStart(2, '0'); subtleOut = h; });",
                  "t", &err),
              "subtle expression evals");
        QString pumpErr;
        bool settled = false;
        QDeadlineTimer drain(10000);
        while (!drain.hasExpired() && !settled) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            while (host.engine()->executePendingJobs(&pumpErr)) {
            }
            settled = host.engine()->globalToJson("subtleOut") !=
                      "\"pending\"";
        }
        CHECK(settled, "subtle promise settles via job pump");
        CHECK(host.engine()->globalToJson("subtleOut") ==
                  "\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb41"
                  "0ff61f20015ad\"",
              "subtle digest value correct");
    }

    { // cheerio through the polyfill wrapper
        PluginHost host;
        QString err;
        CHECK(host.setup("s", "{}", {}, {}, &err), "host for dom");
        QString out;
        CHECK(host.engine()->evalToString(
                  "var $ = require('cheerio').load("
                  "'<ul><li><a href=\"https://a.example/x\">Alpha</a></li>"
                  "<li><a href=\"https://b.example/y\">Beta</a></li></ul>');"
                  "$('a').length + ':' + $('a').first().text() + ':' + "
                  "$('a').eq(1).attr('href')",
                  "t", &out) &&
                  out == "\"2:Alpha:https://b.example/y\"",
              "cheerio select/text/attr");
        CHECK(host.engine()->evalToString(
                  "var $2 = require('cheerio').load("
                  "'<div><p class=\"n\">One</p><p class=\"n\">Two</p></div>');"
                  "$2('p.n').map(function(i, el) { return el.text(); })"
                  ".toArray().join('|')",
                  "t", &out) &&
                  out == "\"One|Two\"",
              "cheerio map/toArray");
    }

    { // async fetch through the full polyfill fetch()
        FetchStub stub;
        CHECK(stub.start(), "fetch stub listens");
        PluginHost host;
        QString err;
        CHECK(host.setup("s", "{}", {}, {}, &err), "host for fetch");
        CHECK(host.engine()->eval(
                  "var fetchOut = 'pending'; fetch('" + stub.baseUrl() +
                      "/streams').then(function(r) { return r.json(); }).then("
                      "function(j) { fetchOut = JSON.stringify(j); }).catch("
                      "function(e) { fetchOut = 'ERR:' + e; });",
                  "t", &err),
              "fetch expression evals");
        QString pumpErr;
        bool settled = false;
        QDeadlineTimer drain(15000);
        while (!drain.hasExpired() && !settled) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            while (host.engine()->executePendingJobs(&pumpErr)) {
            }
            settled = host.engine()->globalToJson("fetchOut") !=
                      "\"pending\"";
        }
        CHECK(settled, "fetch promise settles (jobs + qt events pumped)");
        CHECK(stub.hits == 1, "one native request");
        CHECK(stub.lastMethod == "GET", "fetch rides GET");
        CHECK(host.engine()->globalToJson("fetchOut").contains(
                  "https://cdn.example/a.mkv"),
              "fetch body round-trips through Response.json()");
    }

    std::printf(failures ? "HOST SUITE FAILURES=%d\n"
                         : "HOST SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
