// OFFLINE home-catalog sync tests (codec round-trip, pull apply rules,
// push gating + merged push, signed-out no-ops). ISOLATION: XDG sandbox.
#include <nuvio/authsync/HomeCatalogSyncController.h>

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

#include <nuvio/library/HomeCatalogSync.h>
#include <nuvio/library/HomeShelves.h>

using nuvio::authsync::AuthConfig;
using nuvio::authsync::HomeCatalogSyncController;
using nuvio::library::SyncCatalogItem;
using nuvio::library::SyncHomeCatalogPayload;

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
        if (path.endsWith("sync_pull_home_catalog_settings"))
            payload = pullReply;
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

    { // T1: pure codec - round-trip, defaults, key helpers, local-wins.
        SyncCatalogItem item;
        item.addonId = "com.example";
        item.type = "movie";
        item.catalogId = "top";
        item.order = 3;
        const SyncCatalogItem back =
            SyncCatalogItem::fromJson(item.toJson());
        CHECK(back.addonId == "com.example" && back.type == "movie" &&
                  back.catalogId == "top" && back.enabled && back.order == 3 &&
                  back.customTitle.isEmpty() && !back.isCollection &&
                  back.key.isEmpty(),
              "item round-trip with encodeDefaults");
        CHECK(nuvio::library::preferenceKeyFor(item) ==
                  "com.example:movie:top",
              "preference key composition");
        SyncCatalogItem coll;
        coll.isCollection = true;
        coll.collectionId = "c9";
        CHECK(nuvio::library::preferenceKeyFor(coll) == "collection_c9",
              "collection preference key");
        SyncCatalogItem keyed;
        keyed.key = "explicit";
        keyed.addonId = "ignored";
        CHECK(nuvio::library::preferenceKeyFor(keyed) == "explicit",
              "explicit key wins");
        CHECK(nuvio::library::addonIdForSyncKey(
                  "a:b:extra:movie:top", "movie", "top") == "a:b:extra",
              "colon manifest id survives suffix strip");
        const auto legacy =
            nuvio::library::decomposeLegacyKey("a:movie:top");
        CHECK(legacy.addonId == "a" && legacy.type == "movie" &&
                  legacy.catalogId == "top",
              "legacy split");
        CHECK(nuvio::library::requiresExplicitSyncKey("a:b:c:d") &&
                  !nuvio::library::requiresExplicitSyncKey("a:b:c") &&
                  !nuvio::library::requiresExplicitSyncKey("collection_x"),
              "explicit sync key rule");
        const QJsonObject merged = nuvio::library::mergeSyncJson(
            QJsonObject{{"a", 1}, {"b", 2}}, QJsonObject{{"b", 3}});
        CHECK(merged.value("a").toInt() == 1 &&
                  merged.value("b").toInt() == 3,
              "merge keeps remote extras, local wins");
        const SyncHomeCatalogPayload decoded =
            SyncHomeCatalogPayload::fromJson(QJsonObject{}, true, false);
        CHECK(decoded.showCatalogType && !decoded.hideUnreleasedContent &&
                  decoded.items.isEmpty(),
              "missing flags preserve local defaults");
    }

    FakeRpc rpc;
    if (!rpc.start()) return 2;
    AuthConfig cfg;
    cfg.anonKey = "test-anon";
    cfg.baseUrl = rpc.baseUrl().toString().toUtf8();

    nuvio::library::HomeShelves shelves(nullptr);
    HomeCatalogSyncController sync(cfg, [] { return QByteArray("jwt"); },
                                   [] { return QStringLiteral("user-1"); },
                                   &shelves, 1);
    sync.setDebounceMs(10);
    int pushes = 0, pulls = 0;
    bool lastApplied = false;
    QObject::connect(&sync, &HomeCatalogSyncController::pushFinished,
                     [&](bool) { ++pushes; });
    QObject::connect(&sync, &HomeCatalogSyncController::pullFinished,
                     [&](bool, bool applied) {
                         ++pulls;
                         lastApplied = applied;
                     });

    { // T2: push stays gated before the initial pull completes.
        shelves.setHideUnreleasedContent(true);
        sync.onLocalCatalogChanged();
        pump(120);
        CHECK(pushes == 0 && rpc.paths.isEmpty(), "no push before pull");
        shelves.setHideUnreleasedContent(false);
    }

    { // T3: pull applies remote flags + items.
        rpc.pullReply = QByteArray(
            "[{\"profile_id\":1,\"settings_json\":{\"show_catalog_type\":"
            "false,\"hide_unreleased_content\":true,\"items\":[{"
            "\"addon_id\":\"a\",\"type\":\"movie\",\"catalog_id\":\"top\","
            "\"enabled\":true,\"order\":0,\"custom_title\":\"\",\"is_"
            "collection\":false,\"collection_id\":\"\",\"key\":\"\"}]}}]");
        sync.pullNow();
        pump(250);
        CHECK(pulls == 1 && lastApplied, "pull applied");
        CHECK(!shelves.showCatalogType() && shelves.hideUnreleasedContent(),
              "remote flags applied");
        const SyncHomeCatalogPayload exported = shelves.exportSyncPayload();
        CHECK(exported.items.size() == 1 &&
                  exported.items[0].addonId == "a" &&
                  exported.items[0].type == "movie" &&
                  exported.items[0].catalogId == "top",
              "remote items applied with legacy decomposition");
    }

    { // T4: local edit debounces into a merged push (local wins).
        const int pushesBefore = pushes;
        shelves.setShowCatalogType(true);
        sync.onLocalCatalogChanged();
        pump(250);
        CHECK(pushes == pushesBefore + 1, "debounced push fired");
        CHECK(rpc.paths.last().endsWith("sync_push_home_catalog_settings"),
              "push rpc path");
        const auto params =
            QJsonDocument::fromJson(rpc.bodies.last()).object();
        CHECK(params.value(QStringLiteral("p_profile_id")).toInt() == 1,
              "profile param");
        CHECK(params.value(QStringLiteral("p_platform")).toString() ==
                  "home_catalog_shared",
              "shared platform param");
        const auto pushed =
            params.value(QStringLiteral("p_settings_json")).toObject();
        CHECK(pushed.value(QStringLiteral("show_catalog_type")).toBool(),
              "local flag wins the merge");
        CHECK(pushed.value(QStringLiteral("hide_unreleased_content"))
                  .toBool(),
              "remote flag rides along");
        CHECK(params.contains(QStringLiteral("p_origin_client_id")),
              "origin id present");
    }

    { // T5: empty-items remote applies flags only, keeps local order.
        rpc.pullReply = QByteArray(
            "[{\"profile_id\":1,\"settings_json\":{\"show_catalog_type\":"
            "true,\"hide_unreleased_content\":false,\"items\":[]}}]");
        sync.pullNow();
        pump(250);
        CHECK(pulls == 2, "empty-items pull finished");
        const SyncHomeCatalogPayload exported = shelves.exportSyncPayload();
        CHECK(exported.items.size() == 1 &&
                  exported.items[0].addonId == "a",
              "local items preserved on empty remote");
        CHECK(exported.showCatalogType && !exported.hideUnreleasedContent,
              "flags still applied");
    }

    { // T6: absent blob preserves local state but completes the pull.
        shelves.setShowCatalogType(false);
        rpc.pullReply = QByteArray("[]");
        sync.pullNow();
        pump(250);
        CHECK(pulls == 3 && !lastApplied, "null blob preserves local");
        CHECK(!shelves.showCatalogType(), "local flag untouched");
        const int pushesBefore = pushes;
        sync.onLocalCatalogChanged();
        pump(250);
        CHECK(pushes == pushesBefore + 1, "push ungated after null pull");
    }

    { // T7: profile switch re-gates pushes until the new pull.
        sync.setProfileId(2);
        const int pushesBefore = pushes;
        sync.onLocalCatalogChanged();
        pump(120);
        CHECK(pushes == pushesBefore, "push gated after profile switch");
        sync.setProfileId(1);
    }

    { // T8: signed-out controller is a full no-op.
        FakeRpc quiet;
        CHECK(quiet.start(), "quiet endpoint listens");
        cfg.baseUrl = quiet.baseUrl().toString().toUtf8();
        HomeCatalogSyncController anon(cfg, [] { return QByteArray(); },
                                       [] { return QString(); }, &shelves, 1);
        anon.setDebounceMs(10);
        anon.pullNow();
        anon.pushNow();
        anon.onLocalCatalogChanged();
        pump(60);
        CHECK(quiet.paths.isEmpty(), "no request without a token");
    }

    std::printf(failures ? "HOMECATALOG-SYNC SUITE FAILURES=%d\n"
                         : "HOMECATALOG-SYNC SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
