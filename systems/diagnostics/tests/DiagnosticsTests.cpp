// OFFLINE diagnostics tests (metadata buckets, sanitizer drops, DSN +
// envelope shapes, settings round-trip, client transport against a local
// HTTP fake). ISOLATION: XDG sandbox (config + cache). Signal/terminate
// hooks are NOT exercised (they would kill the suite by design - proven
// instead by a scratch crasher, see PLAN).
#include <nuvio/diagnostics/SentryClient.h>
#include <nuvio/diagnostics/SentryEnvelope.h>
#include <nuvio/diagnostics/SentryMetadata.h>
#include <nuvio/diagnostics/SentrySanitizer.h>
#include <nuvio/diagnostics/SentrySettings.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

using nuvio::diagnostics::SentryClient;
using nuvio::diagnostics::SentrySettings;

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

class FakeHttp final : public QObject {
public:
    QList<QByteArray> paths, bodies, authHeaders;

    bool start()
    {
        if (!m_srv.listen(QHostAddress::LocalHost)) return false;
        m_port = m_srv.serverPort();
        connect(&m_srv, &QTcpServer::newConnection, this,
                [this] { accept(); });
        return true;
    }
    quint16 port() const { return m_port; }

private:
    void accept()
    {
        auto* sock = m_srv.nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            m_buf[sock] += sock->readAll();
            const int hdrEnd = m_buf[sock].indexOf("\r\n\r\n");
            if (hdrEnd < 0) return;
            const QByteArray head = m_buf[sock].left(hdrEnd);
            const QList<QByteArray> lines = head.split('\n');
            paths << lines.value(0).split(' ').value(1);
            for (int i = 1; i < lines.size(); ++i) {
                const QByteArray h = lines[i].trimmed();
                if (h.startsWith("X-Sentry-Auth:"))
                    authHeaders << h.mid(14).trimmed();
            }
            const QByteArray replyBody = "{}";
            int contentLength = 0;
            for (int i = 1; i < lines.size(); ++i) {
                const QByteArray h = lines[i].trimmed();
                if (h.startsWith("Content-Length:"))
                    contentLength = h.mid(15).trimmed().toInt();
            }
            if (m_buf[sock].size() - (hdrEnd + 4) < contentLength) return;
            bodies << m_buf[sock].mid(hdrEnd + 4);
            QByteArray out = "HTTP/1.1 200 OK\r\nContent-Type: "
                             "application/json\r\nConnection: close\r\n"
                             "Content-Length: 2\r\n\r\n{}";
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
    qputenv("XDG_CACHE_HOME",
            QDir(sandbox.path()).filePath("cache").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CACHE_HOME")));

    { // T1: metadata buckets (DesktopSentryMetadata parity).
        using nuvio::diagnostics::normalizeSentryArchitecture;
        using nuvio::diagnostics::normalizeSentryPlatform;
        using nuvio::diagnostics::versionCodeFromString;
        CHECK(normalizeSentryPlatform("Linux") == "linux", "linux bucket");
        // Upstream order quirk, pinned: "darwin" contains "win".
        CHECK(normalizeSentryPlatform("Darwin") == "windows",
              "darwin quirk");
        CHECK(normalizeSentryArchitecture("amd64") == "x86_64", "amd64");
        CHECK(normalizeSentryArchitecture("aarch64") == "arm64", "arm64");
        CHECK(normalizeSentryArchitecture("i686") == "x86", "i686");
        CHECK(normalizeSentryArchitecture("") == "unknown", "blank arch");
        CHECK(versionCodeFromString("0.1.20.0") == 12000, "version code");
        CHECK(nuvio::diagnostics::sentryRelease("pkg", "1.2.3", 4) ==
                  "pkg@1.2.3+4",
              "release shape");
        CHECK(nuvio::diagnostics::sentryDistribution(4, "linux",
                                                     "x86_64") ==
                  "4-linux-x86_64",
              "dist shape");
    }

    { // T2: sanitizer (SentryEventSanitizer parity).
        using nuvio::diagnostics::shouldDropSentryEvent;
        CHECK(nuvio::diagnostics::sentryIgnoredIssueTexts().size() == 2,
              "two ignored texts");
        CHECK(shouldDropSentryEvent({"boom: File IO on Main Thread x"}),
              "ignored fragment drops");
        CHECK(!shouldDropSentryEvent({"Null deref in shelf render"}),
              "real crash kept");
        CHECK(!shouldDropSentryEvent({}), "empty kept");
    }

    { // T3: DSN + envelope shapes.
        using nuvio::diagnostics::buildSentryEnvelope;
        using nuvio::diagnostics::buildSentryEventJson;
        using nuvio::diagnostics::newSentryEventId;
        using nuvio::diagnostics::parseSentryDsn;
        using nuvio::diagnostics::sentryAuthHeader;
        using nuvio::diagnostics::sentryEnvelopeUrl;
        using nuvio::diagnostics::SentryDsn;
        using nuvio::diagnostics::SentryEvent;
        const SentryDsn dsn =
            parseSentryDsn("https://pubkey@example.com:9000/42");
        CHECK(dsn.valid() && dsn.host == "example.com" && dsn.port == 9000 &&
                  dsn.publicKey == "pubkey" && dsn.projectId == "42",
              "dsn parsed");
        CHECK(!parseSentryDsn("").valid(), "blank dsn invalid");
        CHECK(!parseSentryDsn("not-a-dsn").valid(), "garbage dsn invalid");
        CHECK(!parseSentryDsn("ftp://k@h/1").valid(), "scheme gated");
        CHECK(sentryEnvelopeUrl(dsn) ==
                  "https://example.com:9000/api/42/envelope/",
              "envelope url");
        CHECK(sentryAuthHeader(dsn, "c").startsWith("Sentry sentry_version=7") &&
                  sentryAuthHeader(dsn, "c").contains("sentry_key=pubkey"),
              "auth header");
        CHECK(newSentryEventId().size() == 32, "event id 32 hex");
        SentryEvent dropped;
        dropped.message = "Large HTTP payload skipped";
        CHECK(buildSentryEventJson(dropped).isEmpty(), "drop at build");
        SentryEvent kept;
        kept.message = "Null deref in shelf render";
        kept.release = "pkg@1+1";
        const QJsonObject json = buildSentryEventJson(kept);
        CHECK(!json.isEmpty() && !json.contains("request") &&
                  !json.contains("user") &&
                  !json.contains("serverName") &&
                  json.value("platform").toString() == "native",
              "sanitized shape");
        const QByteArray envelope = buildSentryEnvelope(json);
        CHECK(envelope.count('\n') == 2 &&
                  envelope.contains("\"type\":\"event\""),
              "three-line envelope");
    }

    { // T4: settings round-trip (repository parity: default true).
        SentrySettings settings;
        CHECK(settings.enabled(), "default enabled");
        settings.setEnabled(false);
        CHECK(!SentrySettings{}.enabled(), "persisted off");
        settings.setEnabled(true);
        CHECK(SentrySettings{}.enabled(), "persisted on");
        qunsetenv("NUVIO_SENTRY_DSN");
        CHECK(!SentrySettings{}.supported(), "unsupported without dsn");
    }

    FakeHttp http;
    if (!http.start()) return 2;
    qputenv("NUVIO_SENTRY_DSN",
            QString("http://testkey@127.0.0.1:%1/7")
                .arg(http.port())
                .toUtf8());

    { // T5: enabled client posts the envelope; disabled stays silent.
        CHECK(SentrySettings{}.supported(), "supported with dsn");
        SentryClient client(QStringLiteral("0.1.20.0"),
                            QStringLiteral("test"));
        CHECK(!client.active(), "inactive before start");
        client.start();
        CHECK(client.active(), "active after start");
        client.start();   // idempotent
        client.captureMessage("Null deref in shelf render");
        pump(300);
        CHECK(http.paths.size() == 1 &&
                  http.paths.first().endsWith("/api/7/envelope/"),
              "envelope posted");
        CHECK(http.authHeaders.size() == 1 &&
                  http.authHeaders.first().contains("sentry_key=testkey"),
              "auth header sent");
        const QByteArray body = http.bodies.first();
        const QJsonObject event =
            QJsonDocument::fromJson(body.split('\n').value(2)).object();
        CHECK(event.value("message").toString() ==
                  "Null deref in shelf render",
              "message payload");
        CHECK(event.value("release").toString().startsWith(
                  "io.github.jjdizz1l.NuvioLinux@0.1.20.0+"),
              "release tag");
        CHECK(event.value("environment").toString() == "test",
              "environment");
        CHECK(event.contains("breadcrumbs"), "lifecycle crumb rides along");
        client.setEnabled(false);
        client.captureMessage("Another one");
        pump(200);
        CHECK(http.paths.size() == 1, "disabled client silent");
        client.setEnabled(true);
    }

    { // T6: pending crash markers flush as fatal events on next start.
        QFile marker(QDir(QString::fromUtf8(qgetenv("XDG_CACHE_HOME")))
                         .filePath("nuvio/sentry/pending/4242.signal"));
        QDir().mkpath(QFileInfo(marker).absolutePath());
        CHECK(marker.open(QIODevice::WriteOnly | QIODevice::Truncate),
              "marker staged");
        marker.write("signal=11\n");
        marker.close();
        const int before = http.paths.size();
        SentryClient reboot(QStringLiteral("0.1.20.0"),
                            QStringLiteral("test"));
        reboot.start();
        pump(300);
        CHECK(http.paths.size() == before + 1, "pending crash flushed");
        const QJsonObject event = QJsonDocument::fromJson(
                                      http.bodies.last().split('\n').value(2))
                                      .object();
        CHECK(event.value("exception")
                      .toObject()
                      .value("values")
                      .toArray()
                      .first()
                      .toObject()
                      .value("type")
                      .toString() == "SIGSEGV",
              "signal named");
        CHECK(!QFile::exists(marker.fileName()), "marker consumed");
    }

    std::printf(failures ? "DIAGNOSTICS SUITE FAILURES=%d\n"
                         : "DIAGNOSTICS SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
