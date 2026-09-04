// OFFLINE provider-creds tests (dirty push, seed, remote-wins merge,
// signed-out no-ops). ISOLATION: XDG sandbox.
#include <nuvio/authsync/ProviderCredsController.h>

#include <nuvio/settings/AppSettings.h>
#include <nuvio/mdblist/MdbListSettings.h>
#include <nuvio/tmdb/TmdbSettings.h>

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

using nuvio::authsync::AuthConfig;
using nuvio::authsync::ProviderCredsController;
using nuvio::settings::AppSettings;

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
        if (path.endsWith("sync_pull_provider_credentials"))
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

    FakeRpc rpc;
    if (!rpc.start()) return 2;
    AuthConfig cfg;
    cfg.anonKey = "test-anon";
    cfg.baseUrl = rpc.baseUrl().toString().toUtf8();

    AppSettings settings;
    nuvio::tmdb::TmdbSettings tmdb;
    nuvio::mdblist::MdbListSettings mdblist;
    ProviderCredsController sync(&settings, &tmdb, &mdblist, cfg,
                                 [] { return QByteArray("jwt"); }, 1);
    sync.setDebounceMs(10);
    int syncs = 0;
    QObject::connect(&sync, &ProviderCredsController::syncFinished,
                     [&](bool, bool) { ++syncs; });

    { // T1: empty server + local values -> seed RPC with all rows.
        settings.setAnimeSkipClientId("cid-1");
        settings.setIntroDbApiKey("key-1");
        tmdb.setApiKey("tmdb-1");
        mdblist.setApiKey("mdb-1");
        rpc.pullReply = "[]";
        sync.syncNow();
        pump(300);
        CHECK(syncs == 1, "seed cycle finished");
        bool sawSeed = false;
        QJsonObject seedParams;
        for (int i = 0; i < rpc.paths.size(); ++i) {
            if (rpc.paths[i].endsWith("sync_seed_provider_credentials")) {
                sawSeed = true;
                seedParams =
                    QJsonDocument::fromJson(rpc.bodies[i]).object();
            }
        }
        CHECK(sawSeed, "empty server seeds");
        const auto creds =
            seedParams.value(QStringLiteral("p_credentials")).toArray();
        CHECK(creds.size() == 4, "all four credential rows seeded");
        bool tmdbOk = false;
        bool mdbOk = false;
        for (const auto& v : creds) {
            const auto o = v.toObject();
            const QString provider =
                o.value(QStringLiteral("provider")).toString();
            const QString key = o.value(QStringLiteral("credential_json"))
                                    .toObject()
                                    .value(QStringLiteral("api_key"))
                                    .toString();
            if (provider == "tmdb" && key == "tmdb-1") tmdbOk = true;
            if (provider == "mdblist" && key == "mdb-1") mdbOk = true;
        }
        CHECK(tmdbOk, "tmdb row seeded in field shape");
        CHECK(mdbOk, "mdblist row seeded in field shape");
        CHECK(seedParams.value(QStringLiteral("p_profile_id")).toInt() == 1,
              "profile param");
    }

    { // T2: remote wins per provider on merge.
        rpc.pullReply = QByteArray(
            "[{\"provider\":\"animeskip\",\"credential_json\":"
            "{\"client_id\":\"cid-remote\"}},"
            "{\"provider\":\"introdb\",\"credential_json\":"
            "{\"api_key\":\"key-1\"}},"
            "{\"provider\":\"tmdb\",\"credential_json\":"
            "{\"api_key\":\"tmdb-remote\"}},"
            "{\"provider\":\"mdblist\",\"credential_json\":"
            "{\"api_key\":\"mdb-remote\"}}]");
        sync.syncNow();   // baseline clean -> straight to pull+merge
        pump(300);
        CHECK(syncs == 2, "merge cycle finished");
        CHECK(settings.animeSkipClientId() == "cid-remote",
              "remote animeskip value applied");
        CHECK(settings.introDbApiKey() == "key-1",
              "matching introdb value untouched");
        CHECK(tmdb.apiKey() == "tmdb-remote", "remote tmdb value applied");
        CHECK(mdblist.apiKey() == "mdb-remote",
              "remote mdblist value applied");
    }

    { // T3: local edit pushes (push RPC, credential_json shapes).
        settings.setIntroDbApiKey("key-2");
        sync.onLocalCredsChanged();
        pump(300);
        CHECK(syncs == 3, "dirty push finished");
        bool sawPush = false;
        QJsonObject pushParams;
        for (int i = 0; i < rpc.paths.size(); ++i) {
            if (rpc.paths[i].endsWith("sync_push_provider_credentials")) {
                sawPush = true;
                pushParams =
                    QJsonDocument::fromJson(rpc.bodies[i]).object();
            }
        }
        CHECK(sawPush, "dirty push hit the wire");
        const auto creds =
            pushParams.value(QStringLiteral("p_credentials")).toArray();
        bool introOk = false;
        for (const auto& v : creds) {
            const auto o = v.toObject();
            if (o.value(QStringLiteral("provider")).toString() ==
                    "introdb" &&
                o.value(QStringLiteral("credential_json"))
                        .toObject()
                        .value(QStringLiteral("api_key"))
                        .toString() == "key-2")
                introOk = true;
        }
        CHECK(introOk, "push carries edited key in field shape");
    }

    { // T4: signed-out controller is a full no-op.
        FakeRpc quiet;
        CHECK(quiet.start(), "quiet endpoint listens");
        cfg.baseUrl = quiet.baseUrl().toString().toUtf8();
        ProviderCredsController anon(&settings, &tmdb, &mdblist, cfg,
                                     [] { return QByteArray(); }, 1);
        anon.setDebounceMs(10);
        anon.syncNow();
        anon.onLocalCredsChanged();
        pump(60);
        CHECK(quiet.paths.isEmpty(), "no request without a token");
    }

    std::printf(failures ? "CREDS SUITE FAILURES=%d\n"
                         : "CREDS SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
