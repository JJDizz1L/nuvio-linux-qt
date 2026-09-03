// OFFLINE profile manager tests: local payload, switching incl. PIN
// gating, anonymous CRUD, server pull/push shapes over a TCP fake.
// ISOLATION: XDG sandbox.
#include <nuvio/profiles/ProfileManager.h>

#include <nuvio/profiles/ProfileStore.h>
#include <nuvio/settings/PropertiesStore.h>

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

#include <nuvio/settings/ActiveProfile.h>

using nuvio::profiles::ProfileManager;

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
    // pull_profiles rows served FIFO per call.
    QList<QByteArray> pullRows;
    QByteArray locksReply = "[]";

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
        if (path.endsWith("sync_pull_profiles"))
            payload = pullRows.isEmpty() ? QByteArray("[]")
                                         : pullRows.takeFirst();
        else if (path.endsWith("sync_pull_profile_locks"))
            payload = locksReply;
        else if (path.endsWith("verify_profile_pin"))
            payload = verifyReply;
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

public:
    QByteArray verifyReply = "{\"unlocked\":true}";

private:
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
    nuvio::settings::ActiveProfile::setId(1);

    FakeRpc rpc;
    if (!rpc.start()) return 2;
    nuvio::authsync::AuthConfig cfg;
    cfg.anonKey = "test-anon";
    cfg.baseUrl = rpc.baseUrl().toString().toUtf8();

    // Signed-out manager: fully local (profiles UI gates on sign-in, but
    // the local paths must still behave for offline coverage).
    ProfileManager local(cfg, [] { return QByteArray(); });
    int switches = 0;
    QObject::connect(&local, &ProfileManager::activeProfileChanged,
                     [&](int) { ++switches; });

    { // T1: anonymous create/switch round-trip, first-free index reuse.
        local.setAuthUserId(QString());
        local.loadLocal();
        CHECK(local.profilesVariant().isEmpty(), "fresh payload empty");
        local.createProfile("One", "#FF0000");
        local.createProfile("Two", "");
        CHECK(local.profilesVariant().size() == 2, "two local profiles");
        CHECK(local.profilesVariant()[1].toMap().value("index").toInt() == 2,
              "second takes index 2");
        CHECK(local.profilesVariant()[1].toMap().value("avatarColorHex") ==
                  "#1E88E5",
              "empty color falls back to default");
        local.switchToProfile(2);
        CHECK(local.activeProfileIndex() == 2 && switches == 1 &&
                  nuvio::settings::ActiveProfile::id() == 2,
              "switch flips the active profile globally");
        local.deleteProfile(1);
        local.createProfile("Three", "");
        CHECK(local.profilesVariant()[0].toMap().value("index").toInt() == 1,
              "freed index 1 reused");
    }

    { // T2: PIN gating - locked profiles refuse without verification.
        // Give profile 2 a PIN locally: flip the row pinEnabled through a
        // signed-in-shaped payload is overkill; verify the open path and
        // the unknown-profile refusal instead (server PIN flows need
        // credentials and stay Tier-1 territory).
        bool pinOk = false;
        QString pinMsg;
        QObject::connect(&local, &ProfileManager::pinResult,
                         [&](bool ok, const QString& m) {
                             pinOk = ok;
                             pinMsg = m;
                         });
        local.verifyPin(2, "0000");   // pinEnabled=false locally
        CHECK(pinOk, "pinless profile verifies open");
        local.verifyPin(99, "0000");
        CHECK(!pinOk, "unknown profile refused");
        Q_UNUSED(pinMsg);
        local.switchToProfile(99);
    }

    // Signed-in manager against the fake.
    ProfileManager remote(cfg, [] { return QByteArray("jwt"); });
    int remoteSwitches = 0;
    QObject::connect(&remote, &ProfileManager::activeProfileChanged,
                     [&](int) { ++remoteSwitches; });

    { // T3: server pull adopts rows sorted, keeps stored active when valid.
      // Pre-seed a stored payload with active=2 so the keep-valid branch
      // actually engages (fresh state defaults to 1).
        {
            nuvio::profiles::StoredProfilesPayload seed;
            seed.userId = "user-9";
            seed.activeProfileIndex = 2;
            seed.hasEverSelectedProfile = true;
            nuvio::settings::PropertiesStore store(
                nuvio::settings::PropertiesStore::defaultPath("profiles"));
            store.putString(
                "profiles",
                nuvio::profiles::ProfileCodec::encodeStored(seed)
                    .toStdString());
        }
        remote.setAuthUserId("user-9");
        rpc.pullRows = QList<QByteArray>{
            QByteArray("[{\"id\":\"s1\",\"user_id\":\"user-9\","
                       "\"profile_index\":2,\"name\":\"Remote Two\","
                       "\"avatar_color_hex\":\"#00FF00\"},"
                       "{\"id\":\"s0\",\"user_id\":\"user-9\","
                       "\"profile_index\":1,\"name\":\"Remote One\","
                       "\"avatar_color_hex\":\"#0000FF\"}]"),
        };
        remote.pullProfiles();
        pump(300);
        CHECK(remote.profilesVariant().size() == 2, "two rows pulled");
        CHECK(remote.profilesVariant()[0].toMap().value("index").toInt() == 1,
              "rows sorted by index");
        CHECK(remote.activeProfileIndex() == 2,
              "stored active index kept (valid)");
    }

    { // T4: server push sends the full list + max-profiles + origin.
      // The push auto-refreshes via pull, so stage the refreshed rows
      // BEFORE renaming (FIFO fake serves them to the refresh call).
        rpc.pullRows = QList<QByteArray>{
            QByteArray("[{\"id\":\"s1\",\"user_id\":\"user-9\","
                       "\"profile_index\":2,\"name\":\"Remote Two\"},"
                       "{\"id\":\"s0\",\"user_id\":\"user-9\","
                       "\"profile_index\":1,\"name\":\"Renamed One\"}]"),
        };
        remote.renameProfile(1, "Renamed One");
        pump(400);
        bool sawPush = false;
        QJsonObject pushParams;
        for (int i = 0; i < rpc.paths.size(); ++i) {
            if (rpc.paths[i].endsWith("sync_push_profiles")) {
                sawPush = true;
                pushParams =
                    QJsonDocument::fromJson(rpc.bodies[i]).object();
            }
        }
        CHECK(sawPush, "rename pushed full list");
        CHECK(pushParams.value(QStringLiteral("p_client_max_profiles"))
                      .toInt() == 6,
              "max-profiles param");
        const auto arr =
            pushParams.value(QStringLiteral("p_profiles")).toArray();
        CHECK(arr.size() == 2 &&
                  arr[0].toObject().value(QStringLiteral("name")).toString() ==
                      "Renamed One",
              "push carries edited rows");
        CHECK(remote.profilesVariant().size() == 2 &&
                  remote.profilesVariant()[0].toMap().value("name").toString() ==
                      "Renamed One",
              "post-push refresh adopts server rows");
    }

    { // T5: online PIN verify unlocks, then switching succeeds.
        rpc.verifyReply = "{\"unlocked\":true}";
        bool pinOk = false;
        QObject::connect(&remote, &ProfileManager::pinResult,
                         [&](bool ok, const QString&) { pinOk = ok; });
        // Flip row 2 to pin-gated through a fresh pull.
        rpc.pullRows = QList<QByteArray>{
            QByteArray("[{\"id\":\"s0\",\"user_id\":\"user-9\","
                       "\"profile_index\":1,\"name\":\"Renamed One\"},"
                       "{\"id\":\"s1\",\"user_id\":\"user-9\","
                       "\"profile_index\":2,\"name\":\"Remote Two\","
                       "\"pin_enabled\":true}]"),
        };
        remote.pullProfiles();
        pump(300);
        remote.switchToProfile(2);
        CHECK(remoteSwitches == 0, "gated switch refused without verify");
        remote.verifyPin(2, "1234");
        pump(300);
        CHECK(pinOk, "online verify unlocked");
        remote.switchToProfile(2);
        CHECK(remoteSwitches == 1 && remote.activeProfileIndex() == 2,
              "verified switch succeeds");
    }

    nuvio::settings::ActiveProfile::setId(1);   // leave globals clean
    std::printf(failures ? "PROFILE-MANAGER SUITE FAILURES=%d\n"
                         : "PROFILE-MANAGER SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
