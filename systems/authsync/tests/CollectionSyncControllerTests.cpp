// OFFLINE collections sync tests (full pull replace, debounced full push,
// signed-out no-ops). ISOLATION: XDG sandbox.
#include <nuvio/authsync/CollectionSyncController.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include <nuvio/library/CollectionStore.h>

using nuvio::authsync::AuthConfig;
using nuvio::authsync::CollectionSyncController;
using nuvio::library::CollectionStore;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {

class FakeRpc final : public QObject {
public:
    QList<QByteArray> paths, bodies;
    QByteArray pullReply = "[]";

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
        if (hdrEnd < 0 || buf.endsWith('\n')) return;
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
        const QByteArray path = rl.value(1);
        paths << path;
        bodies << buf.mid(hdrEnd + 4);

        QByteArray payload = "{}";
        if (path.endsWith("sync_pull_collections")) payload = pullReply;
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

    FakeRpc rpc;
    if (!rpc.start()) return 2;
    AuthConfig cfg;
    cfg.anonKey = "test-anon";
    cfg.baseUrl = rpc.baseUrl().toString().toUtf8();

    CollectionSyncController sync(cfg, [] { return QByteArray("jwt"); }, 1);
    sync.setDebounceMs(10);
    int pushes = 0, pulls = 0;
    QObject::connect(&sync, &CollectionSyncController::pushFinished,
                     [&](bool) { ++pushes; });
    QObject::connect(&sync, &CollectionSyncController::pullFinished,
                     [&](bool, bool) { ++pulls; });

    { // T1: full pull replaces local state (verbatim fidelity).
        rpc.pullReply = QByteArray(
            "[{\"id\":\"c1\",\"title\":\"Remote\",\"pinToTop\":true,"
            "\"folders\":[{\"id\":\"f1\",\"title\":\"F\","
            "\"sources\":[{\"provider\":\"addon\",\"addonId\":\"a\","
            "\"type\":\"movie\",\"catalogId\":\"top\"}]}]}]");
        sync.pullNow();
        pump(250);
        CHECK(pulls == 1, "pull finished");
        CollectionStore view(1);
        CHECK(view.collections().size() == 1 &&
                  view.collections()[0].title == "Remote" &&
                  view.collections()[0].pinToTop &&
                  view.collections()[0].folders.size() == 1 &&
                  view.collections()[0].folders[0].addonSources.size() == 1,
              "remote collections applied");
    }

    { // T2: local edit debounces into a full verbatim push.
        CollectionStore local(1);
        local.renameCollection("c1", "Renamed");
        const int pushesBefore = pushes;
        sync.onLocalCollectionsChanged();
        pump(250);
        CHECK(pushes == pushesBefore + 1, "debounced push fired");
        const auto params =
            QJsonDocument::fromJson(rpc.bodies.last()).object();
        CHECK(params.value(QStringLiteral("p_profile_id")).toInt() == 1,
              "profile param");
        const auto arr = params.value(QStringLiteral("p_collections_json"))
                             .toArray();
        CHECK(arr.size() == 1 &&
                  arr[0].toObject().value(QStringLiteral("title")).toString() ==
                      "Renamed",
              "push carries the full edited export");
        CHECK(params.contains(QStringLiteral("p_origin_client_id")),
              "origin id present");
    }

    { // T3: signed-out controller is a full no-op.
        FakeRpc quiet;
        CHECK(quiet.start(), "quiet endpoint listens");
        cfg.baseUrl = quiet.baseUrl().toString().toUtf8();
        CollectionSyncController anon(cfg, [] { return QByteArray(); }, 1);
        anon.setDebounceMs(10);
        anon.pullNow();
        anon.pushNow();
        anon.onLocalCollectionsChanged();
        pump(60);
        CHECK(quiet.paths.isEmpty(), "no request without a token");
    }

    std::printf(failures ? "COLLECTION-SYNC SUITE FAILURES=%d\n"
                         : "COLLECTION-SYNC SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
