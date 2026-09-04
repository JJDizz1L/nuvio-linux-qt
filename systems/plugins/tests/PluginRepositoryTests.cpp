// Offline contract for the plugin repository: URL/manifest rules,
// store round-trip, add/refresh/toggle flows through a local stub,
// parallel execution, and server pull/push through a local RPC fake.
// No external network.
#include <nuvio/authsync/AuthService.h>
#include <nuvio/plugins/PluginRepository.h>
#include <nuvio/plugins/PluginRuntime.h>
#include <nuvio/settings/PropertiesStore.h>
#include <nuvio/tmdb/TmdbService.h>
#include <nuvio/tmdb/TmdbSettings.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::authsync::AuthService;
using nuvio::plugins::currentPluginPlatformTags;
using nuvio::plugins::encodeUnsafeHttpUrlCharacters;
using nuvio::plugins::isPluginRepositoryRefreshDue;
using nuvio::plugins::ManifestError;
using nuvio::plugins::normalizeManifestUrl;
using nuvio::plugins::normalizePluginType;
using nuvio::plugins::parsePluginManifest;
using nuvio::plugins::pluginContentId;
using nuvio::plugins::PluginRepository;
using nuvio::tmdb::TmdbService;
using nuvio::tmdb::TmdbSettings;

namespace {

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Serves a manifest + scraper code, and records RPC traffic.
class Stub final : public QObject {
public:
    QString manifestBody = "{}";
    QString codeBody = "";
    int manifestHits = 0;
    int codeHits = 0;
    QStringList rpcPaths;
    QByteArray lastPushBody;

    bool start()
    {
        connect(&m_srv, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* sock = m_srv.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                m_buf[sock] += sock->readAll();
                const int split = m_buf[sock].indexOf("\r\n\r\n");
                if (split < 0) return;
                int contentLength = 0;
                const QRegularExpression cl(
                    QStringLiteral("Content-Length:\\s*(\\d+)"));
                if (const auto m = cl.match(QString::fromUtf8(
                        m_buf[sock].left(split)));
                    m.hasMatch())
                    contentLength = m.captured(1).toInt();
                if (m_buf[sock].size() < split + 4 + contentLength) return;
                const QByteArray raw = m_buf[sock];
                m_buf.remove(sock);
                const QString head =
                    QString::fromUtf8(raw.left(split)).section("\r\n", 0, 0);
                const QString path =
                    head.section(' ', 1, 1).section('?', 0, 0);
                QByteArray body;
                if (path.endsWith("/manifest.json")) {
                    ++manifestHits;
                    body = manifestBody.toUtf8();
                } else if (path.endsWith(".js")) {
                    ++codeHits;
                    body = codeBody.toUtf8();
                } else if (path.contains("/rpc/sync_push_plugins")) {
                    rpcPaths.append(path);
                    lastPushBody = raw.mid(split + 4);
                    body = "{}";
                } else if (path.startsWith("/rest/v1/plugins")) {
                    rpcPaths.append(path);
                    body = "[]";
                } else {
                    body = "{}";
                }
                const QByteArray out =
                    QByteArray("HTTP/1.1 200 OK\r\nContent-Type: "
                               "application/json\r\nContent-Length: ") +
                    QByteArray::number(body.size()) +
                    QByteArray("\r\nConnection: close\r\n\r\n") + body;
                sock->write(out);
                sock->flush();
                sock->disconnectFromHost();
            });
        });
        if (!m_srv.listen(QHostAddress::LocalHost, 0)) return false;
        m_port = m_srv.serverPort();
        qputenv("NUVIO_SUPABASE_URL",
                QStringLiteral("http://127.0.0.1:%1")
                    .arg(m_port)
                    .toUtf8());
        qputenv("NUVIO_SUPABASE_ANON_KEY", "test-anon");
        return true;
    }

    [[nodiscard]] QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
    }

private:
    QTcpServer m_srv;
    QHash<QTcpSocket*, QByteArray> m_buf;
    quint16 m_port = 0;
};

void seedSignedIn()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("auth"));
    store.putString("access_token", "tok");
    store.putString("refresh_token", "ref");
    store.putString("user_email", "fan@nuvio.tv");
    store.putString("user_id", "u1");
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

    { // pure rules
        CHECK(normalizeManifestUrl("example.com/repo") ==
                  "https://example.com/repo/manifest.json",
              "scheme default + suffix");
        CHECK(normalizeManifestUrl(
                  "https://example.com/repo/manifest.json?tok=1#frag") ==
                  "https://example.com/repo/manifest.json?tok=1",
              "suffix kept, fragment stripped, query kept");
        CHECK(normalizeManifestUrl("  ").isEmpty(), "blank rejected");
        QString err;
        CHECK(normalizeManifestUrl("", &err).isEmpty() &&
                  !err.isEmpty(),
              "blank reports a message");
        CHECK(encodeUnsafeHttpUrlCharacters("a b\"c<d>e\\f^g`h{i|j}") ==
                  "a%20b%22c%3Cd%3Ee%5Cf%5Eg%60h%7Bi%7Cj%7D",
              "unsafe table verbatim");
        CHECK(pluginContentId("tt123", -1, -1) == "tt123", "plain id");
        CHECK(pluginContentId("tmdb:550", -1, -1) == "550",
              "tmdb prefix stripped");
        CHECK(pluginContentId("tt123:1:2", 1, 2) == "tt123",
              "episode suffix stripped");
        CHECK(pluginContentId("tt123:1:2", -1, -1) == "tt123:1:2",
              "suffix kept without season");
        CHECK(pluginContentId("kitsu:1/ep", -1, -1) == "kitsu:1",
              "path head kept");
        CHECK(isPluginRepositoryRefreshDue(0, 1000), "never refreshed due");
        CHECK(isPluginRepositoryRefreshDue(1000, 1000 + 6 * 3600 * 1000),
              "6h boundary due");
        CHECK(!isPluginRepositoryRefreshDue(1000, 1000 + 1000),
              "fresh repos quiet");
        const QStringList tags = currentPluginPlatformTags();
        CHECK(tags.contains("desktop") && tags.contains("linux") &&
                  !tags.contains("jvm"),
              "platform tags (no jvm claim)");
        CHECK(normalizePluginType("Show") == "tv", "show folds");
        const auto manifest = parsePluginManifest(
            "{\"name\":\"R\",\"version\":\"1\",\"description\":\"d\","
            "\"scrapers\":[{\"id\":\"s\",\"name\":\"S\",\"version\":\"2\","
            "\"filename\":\"s.js\"}]}");
        CHECK(manifest.name == "R" && manifest.scrapers.size() == 1 &&
                  manifest.scrapers[0].supportedTypes ==
                      QStringList({"movie", "tv"}),
              "manifest parses with type defaults");
        bool threw = false;
        try {
            parsePluginManifest("{\"version\":\"1\",\"scrapers\":[]}");
        } catch (const ManifestError&) {
            threw = true;
        }
        CHECK(threw, "nameless manifest rejected");
        threw = false;
        try {
            parsePluginManifest(
                "{\"name\":\"R\",\"version\":\"1\",\"scrapers\":[]}");
        } catch (const ManifestError&) {
            threw = true;
        }
        CHECK(threw, "providerless manifest rejected");
    }

    { // add flow: manifest + code through the stub, state persists
        Stub stub;
        CHECK(stub.start(), "plugin stub listens");
        stub.manifestBody =
            "{\"name\":\"Repo\",\"version\":\"3\",\"scrapers\":["
            "{\"id\":\"one\",\"name\":\"One\",\"version\":\"1\","
            "\"filename\":\"one.js\",\"supportedTypes\":[\"movie\"],"
            "\"hasSettings\":true},"
            "{\"id\":\"two\",\"name\":\"Two\",\"version\":\"1\","
            "\"filename\":\"https://cdn.example/two.js\","
            "\"supportedPlatforms\":[\"windows\"]}]}";
        stub.codeBody =
            "module.exports.getStreams = async function() { return [{"
            "title:'T', url:'https://cdn.example/v.mkv'}]; };";
        AuthService auth;
        TmdbSettings tmdbSettings;
        TmdbService tmdb(&tmdbSettings);
        PluginRepository repo(&auth, &tmdb);
        repo.setProfileId(11);   // isolated: blocks share the sandbox
        bool added = false;
        QString addMessage;
        QObject::connect(
            &repo, &PluginRepository::addRepositoryFinished,
            [&](bool ok, const QString& message) {
                added = true;
                addMessage = message;
            });
        repo.addRepository(stub.baseUrl() + "/repo");
        QDeadlineTimer drain(15000);
        while (!drain.hasExpired() && !added)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(added && addMessage == "Repo", "add succeeds with repo name");
        CHECK(stub.manifestHits == 1, "manifest fetched once");
        CHECK(stub.codeHits == 1, "windows-gated scraper skipped");
        CHECK(repo.repositories().size() == 1 &&
                  repo.repositories()[0].scraperCount == 1,
              "repo row lands with counts");
        CHECK(repo.scrapers().size() == 1, "one scraper lands");
        const auto scraper = repo.scrapers()[0];
        CHECK(scraper.id.endsWith(":one") && scraper.hasSettings &&
                  scraper.enabled && scraper.manifestEnabled,
              "scraper identity + flags");
        CHECK(scraper.code.contains("getStreams"), "code cached in row");
        CHECK(repo.enabledScrapersForType("movie").size() == 1 &&
                  repo.enabledScrapersForType("series").isEmpty(),
              "type gating");
        // Toggle persists; a fresh instance restores code from cache.
        repo.toggleScraper(scraper.id, false);
        CHECK(repo.enabledScrapersForType("movie").isEmpty(),
              "toggle disables");
        PluginRepository repo2(&auth, &tmdb);
        repo2.setProfileId(11);
        repo2.initialize();
        CHECK(repo2.scrapers().size() == 1 &&
                  !repo2.scrapers()[0].enabled &&
                  repo2.scrapers()[0].code.contains("getStreams"),
              "persisted state restores (toggle + cached code)");
        // Duplicate add refuses honestly.
        added = false;
        repo.addRepository(stub.baseUrl() + "/repo/");
        drain.setRemainingTime(5000);
        while (!drain.hasExpired() && !added)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(added && !addMessage.isEmpty() && stub.manifestHits == 1,
              "duplicate add refused without refetch");
        // Failed manifest surfaces the error.
        added = false;
        stub.manifestBody = "not json{{{";
        repo.addRepository(stub.baseUrl() + "/broken");
        drain.setRemainingTime(10000);
        while (!drain.hasExpired() && !added)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(added && !addMessage.isEmpty(),
              "broken manifest reports honestly");
    }

    { // parallel execution merges rows across scrapers
        Stub stub;
        CHECK(stub.start(), "exec stub listens");
        stub.manifestBody =
            "{\"name\":\"R\",\"version\":\"1\",\"scrapers\":["
            "{\"id\":\"a\",\"name\":\"A\",\"version\":\"1\","
            "\"filename\":\"a.js\"},"
            "{\"id\":\"b\",\"name\":\"B\",\"version\":\"1\","
            "\"filename\":\"b.js\"}]}";
        stub.codeBody =
            "module.exports.getStreams = async function(tmdbId) { return [{"
            "title:'T', url:'https://cdn.example/' + tmdbId + '.mkv'}]; };";
        AuthService auth;
        TmdbSettings tmdbSettings;
        TmdbService tmdb(&tmdbSettings);
        PluginRepository repo(&auth, &tmdb);
        repo.setProfileId(12);   // isolated from the add-flow block
        bool added = false;
        QObject::connect(
            &repo, &PluginRepository::addRepositoryFinished,
            [&](bool, const QString&) { added = true; });
        repo.addRepository(stub.baseUrl() + "/r");
        QDeadlineTimer drain(15000);
        while (!drain.hasExpired() && !added)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(added && repo.scrapers().size() == 2, "two scrapers added");
        bool done = false;
        QList<QVariantMap> merged;
        // tmdbId passes through (no tmdb key configured, no network).
        repo.executeFor("movie", "tt999", -1, -1,
                        [&](const auto& rows) {
                            for (const auto& r : rows)
                                merged.append(
                                    nuvio::plugins::pluginResultToMap(r));
                            done = true;
                        });
        drain.setRemainingTime(30000);
        while (!drain.hasExpired() && !done)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(done && merged.size() == 2, "both scrapers merged");
        CHECK(merged[0].value("url").toString() ==
                  "https://cdn.example/tt999.mkv",
              "content id reaches scraper code");
    }

    { // server pull seeds rows, push carries installs
        Stub stub;
        CHECK(stub.start(), "sync stub listens");
        seedSignedIn();
        AuthService auth;
        auth.restoreSession();
        CHECK(auth.sessionActive(), "seeded session restores");
        TmdbSettings tmdbSettings;
        TmdbService tmdb(&tmdbSettings);
        PluginRepository repo(&auth, &tmdb);
        // Local install first (profile 1 is clean: earlier blocks use
        // 11/12), then pull against an empty server table.
        stub.manifestBody =
            "{\"name\":\"Local\",\"version\":\"1\",\"scrapers\":["
            "{\"id\":\"s\",\"name\":\"S\",\"version\":\"1\","
            "\"filename\":\"s.js\"}]}";
        stub.codeBody = "module.exports.getStreams = async function() { "
                        "return []; };";
        bool added = false;
        QObject::connect(
            &repo, &PluginRepository::addRepositoryFinished,
            [&](bool, const QString&) { added = true; });
        repo.addRepository(stub.baseUrl() + "/local");
        QDeadlineTimer drain(15000);
        while (!drain.hasExpired() && !added)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(added && repo.repositories().size() == 1,
              "local install lands");
        repo.pullFromServer();
        pump(800);
        CHECK(stub.rpcPaths.join(",").contains("plugins"),
              "pull queries the plugins table");
        // Earlier blocks installed profile-1 rows while the server is
        // empty: the fork's seed path pushes local state up.
        CHECK(stub.rpcPaths.join(",").contains("sync_push_plugins"),
              "empty server seeds from local installs");
        CHECK(stub.lastPushBody.contains("manifest.json"),
              "push carries manifest urls");
    }

    std::printf(failures ? "REPO SUITE FAILURES=%d\n"
                         : "REPO SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
