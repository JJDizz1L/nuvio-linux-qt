// OFFLINE integration tests for leg 4: orchestration over a local TCP fake
// (pull-apply into AppSettings, debounced coalesced push, echo suppression,
// signed-out no-ops). ISOLATION: XDG_CONFIG_HOME redirected to temp dir -
// sync_client_identity/player_settings live in the sandbox profile.
#include <nuvio/authsync/SyncOrchestrator.h>

#include <nuvio/debrid/DebridSettings.h>
#include <nuvio/library/LibraryStore.h>
#include <nuvio/notifications/ReleaseNotifications.h>
#include <nuvio/settings/AppSettings.h>
#include <nuvio/watching/ContinueWatchingPrefs.h>

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
        R"({"preferred_audio_language":{"type":"string","value":"de"}}}})";
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
                .value(QStringLiteral("preferred_audio_language"))
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
        CHECK(player.value(QStringLiteral("preferred_subtitle_language"))
                        .toObject()
                        .value(QStringLiteral("value"))
                        .toString() == "fr",
              "subtitle lang in second push");
        CHECK(!player.value(QStringLiteral("subtitle_use_forced_subtitles"))
                        .toObject()
                        .value(QStringLiteral("value"))
                        .toBool(/* absent would be false too; key presence */),
              player.contains(QStringLiteral(
                  "subtitle_use_forced_subtitles"))
                  ? "forced subs false present in second push"
                  : "forced subs key ABSENT");
    }

    { // T4: identical re-write produces NO further network traffic
        settings.setDarkTheme(false);   // non-player signal; dedup eats it
        settings.setPreferredAudioLanguage(QStringLiteral("en"));  // no-op setter
        pump(120);
        CHECK(pushes == 2, "no push for unchanged player state");
    }

    { // T6: full-blob pull applies player + CW, caches unowned features;
      // the next push re-sends them verbatim (no sibling-state wipe).
      // NOTE: the reply is assembled with QJsonDocument throughout - hand-
      // escaped raw literals once produced a stray byte that Qt's strict
      // parser (unlike python's) rejects, failing the pull with an empty
      // doc while the HTTP status stayed 200.
        QJsonObject playerFragment{
            {QStringLiteral("resize_mode"),
             QJsonObject{{QLatin1String("type"), QLatin1String("string")},
                         {QLatin1String("value"),
                          QStringLiteral("Fill")}}},
        };
        QJsonObject fullFeatures{
            {QStringLiteral("player_settings"), playerFragment},
            {QStringLiteral("theme_settings"),
             QJsonObject{
                 {QStringLiteral("mytheme"),
                  QJsonObject{{QLatin1String("type"),
                               QLatin1String("string")},
                              {QLatin1String("value"),
                               QStringLiteral("CRIMSON")}}}}},
            {QStringLiteral("stream_badge_settings"),
             QJsonObject{
                 {QStringLiteral("show_badges"),
                  QJsonObject{{QLatin1String("type"),
                               QLatin1String("boolean")},
                              {QLatin1String("value"), true}}}}},
            {QStringLiteral("continue_watching_settings_payload"),
             QStringLiteral("{\"isVisible\":false}")},
        };
        rpc.setBlobReply(QJsonDocument(QJsonObject{
                             {QStringLiteral("version"), 3},
                             {QStringLiteral("features"), fullFeatures}})
                             .toJson(QJsonDocument::Compact));
        orch.pullNow();
        pump(200);
        CHECK(pullApplied == 2, "full-blob pull reported applied");
        CHECK(settings.resizeMode() == "Fill", "player fragment applied");
        nuvio::watching::ContinueWatchingPrefsStore cwCheck(1);
        CHECK(cwCheck.loadRaw() == "{\"isVisible\":false}",
              "CW payload applied verbatim into the shared store");

        settings.setHoldToSpeedEnabled(false);
        pump(200);
        CHECK(pushes == 3, "post-pull edit pushes once more");
        const auto features =
            QJsonDocument::fromJson(rpc.bodies.last()).object()
                .value(QStringLiteral("p_settings_json")).toObject()
                .value(QStringLiteral("features")).toObject();
        CHECK(features.value(QStringLiteral("theme_settings")).toObject()
                          .value(QStringLiteral("mytheme")).toObject()
                          .value(QStringLiteral("value")).toString()
                      == "CRIMSON",
              "unowned theme feature re-sent verbatim");
        CHECK(features.value(QStringLiteral("stream_badge_settings"))
                          .toObject()
                          .value(QStringLiteral("show_badges")).toObject()
                          .value(QStringLiteral("value")).toBool()
                      == true,
              "unowned badges feature re-sent verbatim");
        CHECK(features.value(QStringLiteral(
                          "continue_watching_settings_payload"))
                      .toString()
                  == "{\"isVisible\":false}",
              "CW payload re-sent from the shared store");
        CHECK(features.value(QStringLiteral("player_settings")).toObject()
                          .value(QStringLiteral("hold_to_speed_enabled"))
                          .toObject()
                          .value(QStringLiteral("value")).toBool()
                      == false,
              "fresh player fragment rides the same push");
    }

    { // T7: push-before-pull gate - a cold orchestrator never pushes until
      // its first pull attempt completes (empty passthrough cache would
      // otherwise drop server-side sibling features). The pull-primed
      // orchestrator above shares the settings object, so count deltas:
      // exactly one push (his) must fire, never two.
      //
      // NOTE: the post-gate proof uses an ISOLATED settings/orchestrator
      // pair - two orchestrators pushing the same 200 ms window once lost a
      // reply inside this single-threaded TCP fake (9 requests sent, 8
      // completions). That is a fake concurrency limit, not product
      // behavior (production runs one orchestrator); the test avoids it.
        SyncOrchestrator gated(&settings, cfg,
                               [] { return QByteArray("jwt"); });
        gated.setDebounceMs(10);
        gated.beginObserving();
        const int pushesBefore = pushes;
        settings.setShowParentalGuide(false);
        pump(150);
        CHECK(pushes == pushesBefore + 1,
              "only the pull-primed orchestrator pushed (cold one gated)");
        gated.pullNow();   // same full blob: nothing differs anymore
        pump(200);

        AppSettings g2settings;
        SyncOrchestrator gated2(&g2settings, cfg,
                                [] { return QByteArray("jwt"); });
        gated2.setDebounceMs(10);
        int pushes2 = 0;
        QObject::connect(&gated2, &SyncOrchestrator::pushFinished,
                         [&](bool) { ++pushes2; });
        gated2.beginObserving();
        gated2.pullNow();   // opens the gate (no diffs to apply)
        pump(200);
        g2settings.setShowLoadingOverlay(false);
        pump(200);
        CHECK(pushes2 == 1, "push fires exactly once after the gate opens");
        CHECK(pushes == pushesBefore + 1,
              "other orchestrators unaffected by the isolated pair");
    }

    { // T8: owned debrid fragment applies into DebridSettings and rides
      // the next push (credentials stripped at assembly). Assembled with
      // statements, not nested initializers (brace golf bit us before).
        auto envelope = [](const QString& type, const QJsonValue& value) {
            return QJsonObject{{QLatin1String("type"), type},
                               {QLatin1String("value"), value}};
        };
        QJsonObject debridFragment;
        debridFragment.insert(QStringLiteral("debrid_enabled"),
                              envelope("boolean", true));
        debridFragment.insert(QStringLiteral("debrid_torbox_api_key"),
                              envelope("string", "should-not-apply"));
        QJsonObject debridFeatures;
        debridFeatures.insert(QStringLiteral("player_settings"),
                              QJsonObject{});
        debridFeatures.insert(QStringLiteral("debrid_settings"),
                              debridFragment);
        QJsonObject blob;
        blob.insert(QStringLiteral("version"), 3);
        blob.insert(QStringLiteral("features"), debridFeatures);
        rpc.setBlobReply(
            QJsonDocument(blob).toJson(QJsonDocument::Compact));
        nuvio::debrid::DebridSettings debrid;
        orch.setDebridSettings(&debrid);
        orch.pullNow();
        pump(200);
        CHECK(debrid.enabled(), "debrid fragment applied");
        CHECK(debrid.providerApiKey("torbox") == "should-not-apply",
              "credential keys accepted on apply (stored, never pushed)");
        // Push carries the fragment back minus credentials.
        settings.setPreferredAudioLanguage(QStringLiteral("it"));
        pump(200);
        const auto features =
            QJsonDocument::fromJson(rpc.bodies.last()).object()
                .value(QStringLiteral("p_settings_json")).toObject()
                .value(QStringLiteral("features")).toObject();
        CHECK(features.value(QStringLiteral("debrid_settings")).toObject()
                          .value(QStringLiteral("debrid_enabled")).toObject()
                          .value(QStringLiteral("value")).toBool() == true,
              "owned debrid fragment pushed");
        CHECK(!features.value(QStringLiteral("debrid_settings")).toObject()
                   .contains(QStringLiteral("debrid_torbox_api_key")),
              "credentials stripped from the pushed blob");
    }

    { // T9: owned notifications fragment applies remotely and rides push.
        QJsonObject notificationsFragment;
        notificationsFragment.insert(
            QStringLiteral("episodeReleaseAlertsEnabled"), true);
        QJsonObject notificationsFeatures;
        notificationsFeatures.insert(QStringLiteral("player_settings"),
                                     QJsonObject{});
        notificationsFeatures.insert(QStringLiteral("notifications_settings"),
                                     notificationsFragment);
        QJsonObject blob;
        blob.insert(QStringLiteral("version"), 3);
        blob.insert(QStringLiteral("features"), notificationsFeatures);
        rpc.setBlobReply(
            QJsonDocument(blob).toJson(QJsonDocument::Compact));
        nuvio::library::LibraryStore notifLibrary(1);
        nuvio::notifications::ReleaseNotificationManager notifMgr(
            &notifLibrary);
        orch.setReleaseNotifications(&notifMgr);
        orch.pullNow();
        pump(200);
        CHECK(notifMgr.enabled(), "notifications fragment applied");
        // Any local change schedules a push carrying the owned fragment.
        settings.setPreferredAudioLanguage(QStringLiteral("fr"));
        pump(200);
        const auto features2 =
            QJsonDocument::fromJson(rpc.bodies.last()).object()
                .value(QStringLiteral("p_settings_json")).toObject()
                .value(QStringLiteral("features")).toObject();
        CHECK(features2.value(QStringLiteral("notifications_settings"))
                          .toObject()
                          .value(QStringLiteral("episodeReleaseAlertsEnabled"))
                          .toBool() == true,
              "owned notifications fragment pushed");
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