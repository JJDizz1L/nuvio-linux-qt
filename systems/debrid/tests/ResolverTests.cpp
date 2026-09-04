// DebridResolver + values contract: credential pick, magnet shape,
// premiumize classification, full Torbox flow over a TCP fake, values
// rendering through the real template engine.
#include <nuvio/debrid/DebridResolver.h>
#include <nuvio/debrid/DebridSettings.h>
#include <nuvio/debrid/DebridStreamValues.h>
#include <nuvio/debrid/StreamTemplateEngine.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

using nuvio::debrid::DebridResolver;
using nuvio::debrid::DebridSettings;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {

class FakeDebrid final : public QObject {
public:
    QList<QByteArray> paths;

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
            finishIfComplete(sock);
        });
    }

    void finishIfComplete(QTcpSocket* sock)
    {
        QByteArray& buf = m_buf[sock];
        const int hdrEnd = buf.indexOf("\r\n\r\n");
        if (hdrEnd < 0) return;
        const QByteArray head = buf.left(hdrEnd);
        const QList<QByteArray> lines = head.split('\n');
        int contentLength = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray h = lines[i].trimmed();
            if (h.startsWith("Content-Length:"))
                contentLength = h.mid(15).trimmed().toInt();
        }
        if (buf.size() - (hdrEnd + 4) < contentLength) return;

        const QList<QByteArray> rl = lines.value(0).split(' ');
        // Strip query for routing (requestdl carries ?token=...).
        const QByteArray path = rl.value(1).split('?').value(0);
        paths << path;

        QByteArray payload = "{\"success\":false}";
        if (path.endsWith("/torrents/checkcached"))
            payload = "{\"success\":true,\"data\":{\"abc123\":"
                      "{\"name\":\"F.mkv\",\"size\":10}}}";
        else if (path.endsWith("/torrents/createtorrent"))
            payload = "{\"success\":true,\"data\":{\"torrent_id\":7}}";
        else if (path.endsWith("/torrents/mylist"))
            payload = "{\"success\":true,\"data\":{\"files\":["
                      "{\"id\":3,\"name\":\"Show.S01E02.mkv\",\"size\":500},"
                      "{\"id\":4,\"name\":\"notes.txt\",\"size\":1}]}}";
        else if (path.endsWith("/torrents/requestdl"))
            payload = "{\"success\":true,"
                      "\"data\":\"https://cdn.example/x.mp4\"}";
        QByteArray out = "HTTP/1.1 200 OK\r\nContent-Type: "
                         "application/json\r\nConnection: close\r\n"
                         "Content-Length: ";
        out += QByteArray::number(payload.size());
        out += "\r\n\r\n" + payload;
        sock->write(out);
        sock->flush();
        sock->disconnectFromHost();
        m_buf.remove(sock);
    }

    QTcpServer m_srv;
    QHash<QTcpSocket*, QByteArray> m_buf;
    quint16 m_port = 0;
};

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME",
            QDir(sandbox.path()).filePath("cfg").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

    using namespace nuvio::debrid;

    { // credential pick: preferred-or-first configured visible provider
        DebridSettings s;
        DebridResolver r(&s);
        CHECK(!r.canResolve(), "no keys, no resolve");
        s.setEnabled(true);
        CHECK(!r.canResolve(), "enabled without keys still inert");
        s.setProviderApiKey("realdebrid", "rd-key");
        CHECK(!r.canResolve(),
              "realdebrid alone never auto-resolves (invisible)");
        s.setProviderApiKey("torbox", "tb-key");
        CHECK(r.canResolve(), "torbox key enables resolution");
        CHECK(activeResolverProviderId(s) == "torbox",
              "first configured wins without preference");
        s.setPreferredResolverProviderId("premiumize");
        CHECK(activeResolverProviderId(s) == "torbox",
              "unconfigured preference falls back to first");
        s.setProviderApiKey("premiumize", "pm-key");
        s.setPreferredResolverProviderId("premiumize");
        CHECK(activeResolverProviderId(s) == "premiumize",
              "configured preference wins");
        // Disabled master switch gates everything.
        s.setEnabled(false);
        CHECK(!r.canResolve(), "master switch gates resolution");
        s.setEnabled(true);
    }

    { // magnet shape
        const QString got =
            magnetForHash("ABCDEF", "Show Name");
        const QString want = "magnet:?xt=urn:btih:abcdef&dn=Show%20Name";
        CHECK(got == want,
              "magnet lowercases hash, encodes title");
        CHECK(magnetForHash("abc", "") == "magnet:?xt=urn:btih:abc",
              "bare magnet without title");
    }

    { // premiumize classification
        CHECK(classifyPremiumizeError(401, "") == PremiumizeFailure::Error,
              "401 is terminal");
        CHECK(classifyPremiumizeError(200, "not found in cache") ==
                  PremiumizeFailure::NotCached,
              "cache message maps");
        CHECK(classifyPremiumizeError(500, "boom") == PremiumizeFailure::Stale,
              "other failures are stale");
    }

    { // values + default-template rendering end to end
        StreamTemplateEngine engine;
        DebridSettings s;
        const QVariantMap values =
            debridStreamValues("Dune.mkv", "DebridSource", "torbox");
        const QString name = formatStreamName(engine, s.streamNameTemplate(),
                                              values, "Dune.mkv");
        // No resolution facts on this line: resolution slot empties, the
        // service short name renders, "Instant" suffix stays.
        CHECK(name == "TB Instant", "default template with known provider");
        const QVariantMap anon =
            debridStreamValues("Dune.mkv", "X", "unknown-provider");
        CHECK(formatStreamName(engine, s.streamNameTemplate(), anon,
                               "Dune.mkv") == "Cloud Instant",
              "unknown provider falls back to Cloud");
        CHECK(formatStreamDescription(engine, "", anon, "fallback") ==
                  "fallback",
              "empty description template falls back");
    }

    { // resolver honesty without credentials (no network touched).
      // NOTE: earlier blocks enable debrid in this sandbox, so switch it
      // back off explicitly (shared-profile cross-talk is by design).
        DebridSettings s;
        s.setEnabled(false);
        s.setProviderApiKey("torbox", {});
        DebridResolver r(&s);
        QString reason;
        QObject::connect(&r, &DebridResolver::unavailable,
                         [&](const QString& why) { reason = why; });
        r.resolveTorrent("abc", "T");
        pump(100);
        CHECK(reason == "NoCredential", "disabled reports NoCredential");
    }

    { // full Torbox chain over the fake: check -> create -> files ->
      // select S01E02 -> download link.
        FakeDebrid fake;
        CHECK(fake.start(), "fake listens");
        DebridSettings s;
        s.setEnabled(true);
        s.setProviderApiKey("torbox", "tb-key");
        // NOTE: the credential block above persisted a premiumize key +
        // preference in this sandbox; pin the preference so this flow
        // deterministically exercises Torbox (shared-profile design).
        s.setPreferredResolverProviderId("torbox");
        DebridResolver r(&s);
        r.setEndpointOverride("torbox", fake.baseUrl().toString());
        QString gotUrl, gotProvider, gotReason;
        QObject::connect(
            &r, &DebridResolver::resolved,
            [&](const QString& url, const QString&, const QString& provider) {
                gotUrl = url;
                gotProvider = provider;
            });
        QObject::connect(&r, &DebridResolver::unavailable,
                         [&](const QString& why) { gotReason = why; });
        r.resolveTorrent("abc123", "Show", 1, 2);
        pump(800);
        CHECK(gotUrl == "https://cdn.example/x.mp4" &&
                  gotProvider == "torbox" && gotReason.isEmpty(),
              "torbox chain resolves to the download link");
        CHECK(fake.paths.size() == 4, "four vendor calls issued");
    }

    std::printf(failures ? "RESOLVER SUITE FAILURES=%d\n"
                         : "RESOLVER SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
