// OFFLINE integration tests for leg 4: orchestration over a local TCP fake
// (pull-apply into AppSettings, debounced coalesced push, echo suppression,
// signed-out no-ops). ISOLATION: XDG_CONFIG_HOME redirected to temp dir -
// sync_client_identity/player_settings live in the sandbox profile.
#include <nuvio/authsync/SyncOrchestrator.h>

#include <nuvio/settings/AppSettings.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
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
using nuvio::authsync::SyncOrchestrator;
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

/// Multi-request HTTP fake capturing every RPC body; always answers ok.
class FakeRpc final : public QObject {
public:
    QList<QByteArray> paths, bodies;

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

    void setBlobReply(const QByteArray& rowBody)
    {
        m_payload =
            "[{\"profile_id\":1,\"updated_at\":\"t\",\"settings_json\":"
            + rowBody + "}]";
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
        paths << rl.value(1);
        bodies << buf.mid(hdrEnd + 4);

        sock->write(replyFor(rl.value(1)));
        sock->flush();
        sock->disconnectFromHost();
        m_buf.remove(sock);
    }

    QByteArray replyFor(const QByteArray& path)
    {
        QByteArray payload = "{}";
        if (path.endsWith(SyncPullName))
            payload = m_payload.isEmpty() ? QByteArray("[]") : m_payload;
        QByteArray out = "HTTP/1.1 200 OK\r\nContent-Type: "
                         "application/json\r\nConnection: close\r\n"
                         "Content-Length: ";
        out += QByteArray::number(payload.size());
        out += "\r\n\r\n" + payload;
        return out;
    }

    QTcpServer m_srv;
    QHash<QTcpSocket*, QByteArray> m_buf;
    quint16 m_port = 0;
    QByteArray m_payload;

public:
    static constexpr auto SyncPullName = "sync_pull_profile_settings_blob";
};

/// Bounded event-loop wait (test pacing helper).
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

    // Remote blob carrying ONE player field differing from local defaults.
    QByteArray remoteRow =
        R"({"version":3,"features":{"player_settings":)"
        R"({"preferred_audio_language_1":{"type":"string","value":"de"}}}})";
    rpc.setBlobReply(remoteRow);

    AppSettings settings;
    SyncOrchestrator orch(&settings, cfg,
                          [] { return QByteArray("jwt"); });
    orch.setDebounceMs(10);

    int pulls = 0, pullApplied = 0, pushes = 0, pushOk = 0;
    QObject::connect(&orch, &SyncOrchestrator::pullFinished,
                     [&](bool applied) { ++pulls; if (applied) ++pullApplied; });
    QObject::connect(&orch, &SyncOrchestrator::pushFinished,
                     [&](bool ok) { ++pushes; if (ok) ++pushOk; });
    orch.beginObserving();

    { // T1: startup pull applies the fragment through AppSettings
        CHECK(settings.preferredAudioLanguage() != "de",
              "pre-condition local differs from remote");
        orch.pullNow();
        pump(200);
        CHECK(pullApplied == 1, "pull reported applied");
        CHECK(settings.preferredAudioLanguage() == "de",
              "AppSettings reflects pulled value (same-instance store)");

        pump(60);   // grace window for any scheduled push
        CHECK(pushes == 0, "echo suppressed right after pull");
    }

    { // T2: setter fires one debounced push containing the new value
        settings.setPreferredAudioLanguage(QStringLiteral("en"));
        pump(150);
        CHECK(pushes == 1, "debounced push fired");
        CHECK(pushOk == 1, "push ok");

        const auto params = QJsonDocument::fromJson(rpc.bodies.last())
                                .object();
        const auto pushedValue =
            params.value(QStringLiteral("p_settings_json"))
                .toObject()
                .value(QStringLiteral("features"))
                .toObject()
                .value(QStringLiteral("player_settings"))
                .toObject()
                .value(QStringLiteral("preferred_audio_language_1"))
                .toObject()
                .value(QStringLiteral("value"))
                .toString();
        CHECK(pushedValue == "en", "pushed payload carries new language");
        CHECK(params.value(QStringLiteral("p_platform")).toString()
                  == QStringLiteral("desktop"),
              "platform param desktop");
        CHECK(!params.value(QStringLiteral("p_origin_client_id"))
                       .toString().isEmpty(),
              "origin client id present");
        CHECK(params.value(QStringLiteral("p_profile_id")).toInt() == 1,
              "profile id param");
    }

    { // T3: multi-change coalesce -> exactly one extra push with both values
        settings.setPreferredSubtitleLanguage(QStringLiteral("fr"));
        settings.setUseForcedSubtitles(false);
        pump(200);
        CHECK(pushes == 2, "changes coalesced into second push");
        const auto player =
            QJsonDocument::fromJson(rpc.bodies.last()).object()
                .value(QStringLiteral("p_settings_json")).toObject()
                .value(QStringLiteral("features")).toObject()
                .value(QStringLiteral("player_settings")).toObject();
        CHECK(player.value(QStringLiteral("preferred_subtitle_language_1"))
                        .toObject()
                        .value(QStringLiteral("value"))
                        .toString() == "fr",
              "subtitle lang in second push");
        CHECK(!player.value(QStringLiteral("subtitle_use_forced_subtitles_1"))
                        .toObject()
                        .value(QStringLiteral("value"))
                        .toBool(/* absent would be false too; key presence */),
              player.contains(QStringLiteral(
                  "subtitle_use_forced_subtitles_1"))
                  ? "forced subs false present in second push"
                  : "forced subs key ABSENT");
    }

    { // T4: identical re-write produces NO further network traffic
        settings.setDarkTheme(false);   // non-player signal; dedup eats it
        settings.setPreferredAudioLanguage(QStringLiteral("en"));  // no-op setter
        pump(120);
        CHECK(pushes == 2, "no push for unchanged player state");
    }

    { // T5: signed-out orchestrator is a full no-op
        FakeRpc quiet;
        CHECK(quiet.start(), "quiet endpoint listens");
        cfg.baseUrl = quiet.baseUrl().toString().toUtf8();

        SyncOrchestrator anon(&settings, cfg,
                              [] { return QByteArray(); });
        anon.beginObserving();
        bool pullSeen = false;
        QObject::connect(&anon, &SyncOrchestrator::pullFinished,
                         [&](bool) { pullSeen = true; });
        anon.pullNow();
        pump(30);
        CHECK(pullSeen, "signed-out pull finishes immediately");
        CHECK(quiet.paths.isEmpty(), "no request without a token");
    }

    std::printf(failures ? "SYNC-ORCH SUITE FAILURES=%d\n"
                         : "SYNC-ORCH SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}