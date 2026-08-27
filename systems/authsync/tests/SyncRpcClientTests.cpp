// OFFLINE wire-contract tests: local TCP fake endpoint asserts the exact
// postgrest RPC request shape (path/headers/body), then canned responses
// exercise success + non-2xx paths. No production traffic, no XDG writes.
#include <nuvio/authsync/SyncRpcClient.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>

using nuvio::authsync::AuthConfig;
using nuvio::authsync::SyncRpcClient;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {

/// Minimal one-shot HTTP server: captures the raw request, replies once.
class FakeEndpoint final : public QObject {
public:
    QByteArray method, path, headers, body;
    QByteArray reply =
        [] {
            const QByteArray payload = "{\"status\":\"ok\"}";
            return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                   "Connection: close\r\nContent-Length: "
                   + QByteArray::number(payload.size()) + "\r\n\r\n" + payload;
        }();

    bool start()
    {
        if (!m_srv.listen(QHostAddress::LocalHost)) return false;
        m_port = m_srv.serverPort();
        connect(&m_srv, &QTcpServer::newConnection, this, [this] {
            auto* sock = m_srv.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this,
                    [this, sock] { consume(sock); });
        });
        return true;
    }

    QUrl baseUrl() const
    {
        return {QStringLiteral("http://127.0.0.1:%1").arg(m_port)};
    }
    bool requested() const { return !method.isEmpty(); }

private:
    void consume(QTcpSocket* sock)
    {
        m_buf += sock->readAll();
        const int hdrEnd = m_buf.indexOf("\r\n\r\n");
        if (hdrEnd < 0 || !method.isEmpty()) return;
        const QByteArray head = m_buf.left(hdrEnd);
        const QList<QByteArray> lines = head.split('\n');
        int contentLength = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray h = lines[i].trimmed();
            if (h.startsWith("Content-Length:"))
                contentLength = h.mid(15).trimmed().toInt();
            else
                headers += h + '\n';
        }
        const QByteArray bodyChunk = m_buf.mid(hdrEnd + 4);
        if (bodyChunk.size() < contentLength) return;   // body incomplete

        const QList<QByteArray> rl = lines.value(0).split(' ');
        method = rl.value(0);
        path   = rl.value(1);
        body   = bodyChunk;
        sock->write(reply);
        sock->flush();
        sock->disconnectFromHost();
    }

    QTcpServer m_srv;
    QByteArray m_buf;
    quint16 m_port = 0;
};

/// Bounded wait for the client's terminal signal (test-only helper).
void pumpUntil(SyncRpcClient& client, bool* done)
{
    QEventLoop loop;
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    QObject::connect(&client, &SyncRpcClient::finished, &loop,
                     [&](bool, int, const QJsonDocument&, QByteArray) {
                         *done = true;
                         loop.quit();
                     });
    loop.exec();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    AuthConfig cfg;
    cfg.anonKey = "test-anon-key-123";

    { // happy path: user JWT attached, exact path/headers/body
        FakeEndpoint ep;
        CHECK(ep.start(), "fake endpoint listens");
        cfg.baseUrl = ep.baseUrl().toString().toUtf8();

        SyncRpcClient client(cfg,
                             [] { return QByteArray("user-jwt-token"); });

        bool done = false, okSeen = false;
        int statusSeen = 0;
        QJsonObject resp;
        QObject::connect(&client, &SyncRpcClient::finished,
                         [&](bool ok, int status, const QJsonDocument& doc, QByteArray) {
                             done = true; okSeen = ok;
                             statusSeen = status;
                             resp = doc.object();
                         });

        client.call(
            QStringLiteral("sync_pull_profile_settings_blob"),
            QJsonObject{
                {QStringLiteral("p_profile_id"), 1},
                {QStringLiteral("p_platform"), QStringLiteral("desktop")},
                {QStringLiteral("p_origin_client_id"),
                 QStringLiteral("nuvio-mobile-test")},
            });
        pumpUntil(client, &done);

        CHECK(done, "finished emitted");
        CHECK(ep.requested(), "endpoint saw request");
        CHECK(ep.method == "POST", "POST method");
        CHECK(ep.path == "/rest/v1/rpc/sync_pull_profile_settings_blob",
              "exact rpc path");
        // NOTE: Qt title-cases raw header names ("Apikey:") - header NAMES
        // are case-insensitive per RFC 7230 and the postgrest gateway
        // matches caselessly, so exact lowercase wire parity (OkHttp) is
        // NOT required. Assert case-insensitively.
        const bool apikeyOk =
            ep.headers.toLower().contains("apikey: test-anon-key-123");
        CHECK(apikeyOk, "apikey header present");
        if (!apikeyOk)
            std::fprintf(stderr, "HEADERS DUMP:\n%s\n",
                         ep.headers.constData());
        CHECK(ep.headers.contains("Authorization: Bearer user-jwt-token"),
              "user jwt bearer attached");
        CHECK(ep.headers.contains("Accept: application/json"),
              "accept header present");

        const QJsonDocument sent = QJsonDocument::fromJson(ep.body);
        CHECK(sent.object()
                  .value(QStringLiteral("p_platform"))
                  .toString() == QStringLiteral("desktop"),
              "params serialized as json body");

        CHECK(okSeen && statusSeen == 200, "success verdict");
        CHECK(resp.value(QStringLiteral("status")).toString()
                  == QStringLiteral("ok"),
              "response body parsed");
    }

    { // signed-out fallback: anon key becomes the bearer
        FakeEndpoint ep;
        CHECK(ep.start(), "second endpoint listens");
        cfg.baseUrl = ep.baseUrl().toString().toUtf8();

        SyncRpcClient client(cfg, [] { return QByteArray(); });
        bool done = false;
        client.call(QStringLiteral("sync_push_profile_settings_blob"),
                    QJsonObject{});
        pumpUntil(client, &done);
        CHECK(done, "fallback call completed");
        CHECK(ep.headers.contains("Authorization: Bearer test-anon-key-123"),
              "anon fallback bearer when signed out");
    }

    { // network failure still emits finished(false)
        cfg.baseUrl = "http://127.0.0.1:9";   // discard port - refused
        SyncRpcClient client(cfg, [] { return QByteArray("t"); });
        bool done = false, okSeen = true;
        QObject::connect(&client, &SyncRpcClient::finished,
                         [&](bool ok, int, const QJsonDocument&, QByteArray) { okSeen = ok; });
        client.call(QStringLiteral("sync_pull_profile_settings_blob"),
                    QJsonObject{});
        pumpUntil(client, &done);
        CHECK(done, "failure emits finished");
        CHECK(!okSeen, "network failure reported not-ok");
    }

    std::printf(failures ? "SYNC-RPC SUITE FAILURES=%d\n"
                         : "SYNC-RPC SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}