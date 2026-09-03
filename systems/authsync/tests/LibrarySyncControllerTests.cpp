// OFFLINE library sync tests over a local TCP fake (snapshot paging,
// legacy migration, delta merge skipping pending keys, dirty push/delete
// legs, signed-out no-ops). ISOLATION: XDG sandbox.
#include <nuvio/authsync/LibrarySyncController.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include <nuvio/library/LibraryStore.h>

using nuvio::authsync::AuthConfig;
using nuvio::authsync::LibrarySyncController;
using nuvio::library::LibraryStore;

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
    QByteArray cursorReply = "77";
    QList<QByteArray> pullPages;   // FIFO snapshot pages
    QByteArray deltaReply = "[]";

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
        if (path.endsWith("sync_pull_library"))
            payload = pullPages.isEmpty() ? QByteArray("[]")
                                          : pullPages.takeFirst();
        else if (path.endsWith("sync_get_library_delta_cursor"))
            payload = cursorReply;
        else if (path.endsWith("sync_pull_library_delta"))
            payload = deltaReply;
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

QByteArray syncItem(const char* id, const char* type, const char* name,
                    long long addedAt)
{
    return QByteArray("{\"content_id\":\"") + id + "\",\"content_type\":\"" +
           type + "\",\"name\":\"" + name + "\",\"added_at\":" +
           QByteArray::number(addedAt) + "}";
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

    LibrarySyncController sync(cfg, [] { return QByteArray("jwt"); }, 1);
    sync.setDebounceMs(10);
    int pushes = 0, pulls = 0;
    QObject::connect(&sync, &LibrarySyncController::pushFinished,
                     [&](bool) { ++pushes; });
    QObject::connect(&sync, &LibrarySyncController::pullFinished,
                     [&](bool, int) { ++pulls; });

    { // T1: paged snapshot merge (short page ends), cursor seeded.
        rpc.pullPages = QList<QByteArray>{
            QByteArray("[") + syncItem("tt1", "movie", "One", 1000) +
                QByteArray(",") + syncItem("tt2", "movie", "Two", 2000) +
                QByteArray("]"),
            QByteArray("[]"),
        };
        sync.fullLibrarySyncThenDeltas();
        pump(400);
        CHECK(pulls >= 1, "snapshot pull finished");
        LibraryStore view(1);
        CHECK(view.count() == 2 && view.isInLibrary("movie", "tt1"),
              "snapshot rows merged locally");
        CHECK(view.deltaInitialized() && view.deltaCursorEventId() == 77,
              "delta cursor seeded from bare-Long RPC");
    }

    { // T2: legacy migration - empty server + dirty-free local rows keeps
      // the rows and marks every one dirty (nothing is replaced).
        rpc.pullPages = QList<QByteArray>{QByteArray("[]")};
        const int pullsBefore = pulls;
        sync.fullLibrarySyncThenDeltas();
        pump(400);
        CHECK(pulls > pullsBefore, "migration pull finished");
        LibraryStore view(1);
        CHECK(view.isInLibrary("movie", "tt1") &&
                  view.isInLibrary("movie", "tt2"),
              "local-only rows survive an empty server");
        CHECK(view.pendingUpserts().size() == 2,
              "migrated rows marked dirty for upload");
    }

    { // T3: push clears migration dirt; delta then applies to clean rows.
        const int pushesBefore = pushes;
        sync.pushDirty();
        pump(500);
        CHECK(pushes > pushesBefore, "migration dirt pushed");
        LibraryStore clean(1);
        CHECK(clean.pendingUpserts().isEmpty(), "dirt cleared");

        rpc.deltaReply =
            QByteArray("[{\"event_id\":78,\"operation\":\"upsert\","
                       "\"content_id\":\"tt1\",\"content_type\":\"movie\","
                       "\"name\":\"One (Remote)\",\"added_at\":6000},"
                       "{\"event_id\":79,\"operation\":\"upsert\","
                       "\"content_id\":\"tt10\",\"content_type\":\"movie\","
                       "\"name\":\"Ten\",\"added_at\":7000},"
                       "{\"event_id\":80,\"operation\":\"delete\","
                       "\"content_id\":\"tt2\",\"content_type\":\"movie\","
                       "\"name\":\"\",\"added_at\":0}]");
        // Local edit to tt1 AFTER the push makes it pending again, so the
        // remote upsert for tt1 must lose (pending keys win locally).
        clean.addToLibrary("movie", "tt1", "One (Local)", "", "", 9000);
        const int pullsBefore = pulls;
        sync.pullLibraryDelta();
        pump(300);
        CHECK(pulls > pullsBefore, "delta pull finished");
        LibraryStore view(1);
        {
            QString tt1name;
            for (const auto& it : view.items()) {
                if (it.id == "tt1") tt1name = it.name;
            }
            CHECK(tt1name == "One (Local)", "pending row not clobbered");
        }
        CHECK(view.isInLibrary("movie", "tt10"), "new delta row applied");
        CHECK(!view.isInLibrary("movie", "tt2"), "delta delete applied");
        CHECK(view.deltaCursorEventId() == 80, "cursor tracks max event");
    }

    { // T4: dirty push sends upserts then deletes, clearing both.
        LibraryStore local(1);
        local.addToLibrary("movie", "tt11", "Eleven", "", "", 9000);
        local.removeFromLibrary("movie", "tt10");
        const int pushesBefore = pushes;
        sync.pushDirty();
        pump(500);
        CHECK(pushes > pushesBefore, "dirty push ran both legs");
        bool sawPush = false, sawDelete = false;
        for (const QByteArray& p : rpc.paths) {
            if (p.endsWith("sync_push_library_items")) sawPush = true;
            if (p.endsWith("sync_delete_library_items")) sawDelete = true;
        }
        CHECK(sawPush && sawDelete, "upsert + delete legs hit the wire");
        CHECK(QJsonDocument::fromJson(rpc.bodies.last())
                      .object()
                      .value(QStringLiteral("p_profile_id"))
                      .toInt() == 1,
              "profile param rides along");
        LibraryStore view(1);
        CHECK(view.pendingUpserts().isEmpty() &&
                  view.pendingDeletes().isEmpty(),
              "acknowledged dirt cleared");
    }

    { // T5: signed-out controller is a full no-op.
        FakeRpc quiet;
        CHECK(quiet.start(), "quiet endpoint listens");
        cfg.baseUrl = quiet.baseUrl().toString().toUtf8();
        LibrarySyncController anon(cfg, [] { return QByteArray(); }, 1);
        anon.setDebounceMs(10);
        bool pullSeen = false;
        QObject::connect(&anon, &LibrarySyncController::pullFinished,
                         [&](bool, int) { pullSeen = true; });
        anon.fullLibrarySyncThenDeltas();
        anon.pushDirty();
        pump(60);
        CHECK(quiet.paths.isEmpty(), "no request without a token");
        Q_UNUSED(pullSeen);
    }

    std::printf(failures ? "LIBRARY-SYNC SUITE FAILURES=%d\n"
                         : "LIBRARY-SYNC SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
