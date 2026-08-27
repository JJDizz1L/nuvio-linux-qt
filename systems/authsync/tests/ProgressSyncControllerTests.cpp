// OFFLINE integration: ProgressSyncController push/dirty/delete-legs +
// initial full-pull merge + cursor seeding, against a local TCP fake that
// routes by RPC path. ISOLATION: XDG_CONFIG_HOME redirected to temp.
#include <nuvio/authsync/ProgressSyncController.h>

#include <nuvio/settings/AppSettings.h>
#include <nuvio/watching/WatchingStore.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

using nuvio::authsync::AuthConfig;
using nuvio::authsync::ProgressSyncController;
using nuvio::watching::WatchingStore;
using nuvio::watching::WatchEntry;

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
    QByteArray cursorReply = "1234";
    QByteArray pullReply   = "[]";
    QByteArray deltaReply  = "[]";
    // Sequential pages for watched full-pull; empty -> fall back to pullReply.
    QList<QByteArray> pullPages;
    // Sequential pages served for watched full-pull requests (FIFO).
    QList<QByteArray> watchedPages;

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
        connect(sock, &QTcpSocket::readyRead, this,
                [this, sock] { consume(sock); });
    }

    void consume(QTcpSocket* sock)
    {
        m_buf[sock] += sock->readAll();
        const int hdrEnd = m_buf[sock].indexOf("\r\n\r\n");
        if (hdrEnd < 0) return;
        const QByteArray head = m_buf[sock].left(hdrEnd);
        const QList<QByteArray> lines = head.split('\n');
        int contentLength = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray h = lines[i].trimmed();
            if (h.startsWith("Content-Length:"))
                contentLength = h.mid(15).trimmed().toInt();
        }
        if (m_buf[sock].size() - (hdrEnd + 4) < contentLength) return;

        const QList<QByteArray> rl = lines.value(0).split(' ');
        const QByteArray path = rl.value(1);
        paths << path;
        bodies << m_buf[sock].mid(hdrEnd + 4);

        QByteArray payload = "{}";
        if (path.endsWith("sync_pull_watched_items_delta"))
            payload = deltaReply;
        else if (path.endsWith("sync_pull_watched_items"))
            payload = pullPages.isEmpty()
                          ? pullReply
                          : pullPages.takeFirst();
        else if (path.endsWith("sync_get_watched_items_delta_cursor"))
            payload = cursorReply;
        else if (path.endsWith("sync_get_watch_progress_delta_cursor"))
            payload = cursorReply;
        else if (path.endsWith("sync_pull_watch_progress"))
            payload = pullReply;
        QByteArray out =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Connection: close\r\nContent-Length: ";
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

WatchEntry makeEntry(const char* id, long long pos, long long dur,
                     long long at)
{
    WatchEntry e;
    e.parentMetaId = id;
    e.contentType  = "movie";
    e.videoId      = id;
    e.lastPositionMs = pos;
    e.durationMs     = dur;
    e.lastUpdatedEpochMs = at;
    return e;
}

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
    CHECK(rpc.start(), "fake listens");
    AuthConfig cfg;
    cfg.anonKey = "anon";
    cfg.baseUrl = rpc.baseUrl().toString().toUtf8();

    { // T1: pushDirty pushes ONLY dirty entries + clears dirty marks
        WatchingStore seed(1);
        seed.upsert(makeEntry("ttAAA", 100, 1000, 500));
        seed.upsert(makeEntry("ttBBB", 200, 1000, 600));
        seed.clearProgressDirty({"ttBBB"});   // BBB = clean baseline

        ProgressSyncController ctl(cfg,
                                   [] { return QByteArray("jwt"); }, 1);
        ctl.setDebounceMs(5);
        int pushes = 0, pushedOk = 0;
        QObject::connect(&ctl, &ProgressSyncController::pushFinished,
                         [&](bool ok, int) { ++pushes; if (ok) ++pushedOk; });

        ctl.pushDirty();
        pump(200);
        CHECK(pushes == 1 && pushedOk == 1, "dirty push completed");
        CHECK(rpc.bodies.last().contains("ttAAA"), "dirty entry in payload");
        CHECK(!rpc.bodies.last().contains("ttBBB"),
              "clean entry NOT pushed");

        WatchingStore after(1);
        CHECK(after.loadProgressEnvelope().dirtyProgressKeys.empty(),
              "dirty cleared after successful push");
        CHECK(after.loadProgressEnvelope().lastSuccessfulPushEpochMs > 0,
              "lastSuccessfulPush stamped");
    }

    { // T2: remove marks dirty -> delete leg fires with the key
        WatchingStore store(1);
        store.remove("ttAAA");
        ProgressSyncController ctl(cfg,
                                   [] { return QByteArray("jwt"); }, 1);
        ctl.setDebounceMs(5);
        int pushes = 0;
        QObject::connect(&ctl, &ProgressSyncController::pushFinished,
                         [&](bool, int) { ++pushes; });
        ctl.pushDirty();
        pump(200);
        CHECK(pushes == 1, "delete-leg push finished");
        CHECK(rpc.paths.last().contains("sync_delete_watch_progress"),
              "delete RPC used");
    }

    { // T3: initial full-pull merges newest-wins + seeds cursor
        rpc.pullReply =
            R"([{"content_id":"ttBBB","content_type":"movie",)"
            R"("video_id":"ttBBB","season":null,"episode":null,)"
            R"("position":900,"duration":1000,"last_watched":700,)"
            R"("progress_key":"ttBBB"},)"
            R"({"content_id":"ttCCC","content_type":"movie",)"
            R"("video_id":"ttCCC","season":null,"episode":null,)"
            R"("position":1,"duration":10,"last_watched":800,)"
            R"("progress_key":"ttCCC"}])";
        rpc.cursorReply = "1234";

        ProgressSyncController ctl(cfg,
                                   [] { return QByteArray("jwt"); }, 1);
        int appliedSeen = -1;
        QObject::connect(&ctl, &ProgressSyncController::pullFinished,
                         [&](bool, int applied) { appliedSeen = applied; });
        ctl.fullSyncThenDeltas();
        pump(300);

        CHECK(appliedSeen == 2, "both remote rows applied");
        WatchingStore after(1);
        bool foundB = false;
        for (const auto& e : after.loadEntries())
            if (e.parentMetaId == "ttBBB" &&
                e.lastUpdatedEpochMs == 700)   // newer remote stamp merged
                foundB = true;
        CHECK(foundB, "remote newer row overwrote local");
        const auto env = after.loadProgressEnvelope();
        CHECK(env.deltaCursorEventId == 1234 && env.deltaInitialized,
              "cursor seeded + initialized");
    }

    { // T3b: watched full-pull PAGINATION (2 pages via tiny page size)
        rpc.deltaReply = "[]";
        // page1: 2 full rows; page2: 1 row (short page ends the loop)
        rpc.pullPages.clear();
        QJsonArray p1;
        for (int i = 0; i < 2; ++i)
            p1.append(QJsonObject{
                {QStringLiteral("content_id"),
                 QStringLiteral("ttW%1").arg(i)},
                {QStringLiteral("content_type"), QStringLiteral("movie")},
                {QStringLiteral("title"), QStringLiteral("W%1").arg(i)},
                {QStringLiteral("season"), QJsonValue::Null},
                {QStringLiteral("episode"), QJsonValue::Null},
                {QStringLiteral("watched_at"), 100.0 + i},
                {QStringLiteral("sort_order"), i}});
        QJsonArray p2;
        p2.append(QJsonObject{
            {QStringLiteral("content_id"), QStringLiteral("ttW9")},
            {QStringLiteral("content_type"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("W9")},
            {QStringLiteral("season"), QJsonValue::Null},
            {QStringLiteral("episode"), QJsonValue::Null},
            {QStringLiteral("watched_at"), 300.0},
            {QStringLiteral("sort_order"), 2}});
        rpc.pullPages << QJsonDocument(p1).toJson(QJsonDocument::Compact)
                      << QJsonDocument(p2).toJson(QJsonDocument::Compact);

        ProgressSyncController pc(cfg,
                                  [] { return QByteArray("jwt"); }, 1);
        pc.setWatchedPageSize(2);   // 2 rows per page -> 2 fetches
        int wApplied = -1;
        QObject::connect(&pc, &ProgressSyncController::pullFinished,
                         [&](bool, int a) { wApplied = a; });
        pc.fullWatchedSyncThenDeltas();
        pump(300);

        CHECK(wApplied == 3, "watched pages accumulated (2+1)");
        WatchingStore wAfter(1);
        CHECK(wAfter.loadWatchedItems().size() >= 3,
              "watched rows persisted");
        CHECK(wAfter.watchedDeltaCursorEventId() == 1234 &&
              wAfter.watchedDeltaInitialized(),
              "watched cursor seeded + initialized");
    }

    { // T4: signed-out orchestrator is a no-op
        FakeRpc quiet;
        CHECK(quiet.start(), "quiet endpoint");
        cfg.baseUrl = quiet.baseUrl().toString().toUtf8();
        ProgressSyncController anon(cfg,
                                    [] { return QByteArray(); }, 1);
        int pulls = 0;
        QObject::connect(&anon, &ProgressSyncController::pullFinished,
                         [&](bool, int) { ++pulls; });
        anon.fullSyncThenDeltas();
        pump(50);
        CHECK(pulls == 1 && quiet.paths.isEmpty(),
              "signed-out: finished, zero requests");
    }

    std::printf(failures ? "PROGRESS-CTL SUITE FAILURES=%d\n"
                         : "PROGRESS-CTL SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}