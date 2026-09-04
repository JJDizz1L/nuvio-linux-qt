// Offline contract for TMDB settings + id resolution: key semantics,
// gates, sync envelopes, URL/parse rules, and live mapping through a
// local HTTP stub (XDG-sandboxed; api.themoviedb.org never touched).
#include <nuvio/tmdb/TmdbService.h>
#include <nuvio/tmdb/TmdbSettings.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QHostAddress>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::tmdb::buildTmdbUrl;
using nuvio::tmdb::normalizeLanguage;
using nuvio::tmdb::normalizeMediaType;
using nuvio::tmdb::normalizeVideoId;
using nuvio::tmdb::parseExternalIds;
using nuvio::tmdb::parseFindResult;
using nuvio::tmdb::TmdbService;
using nuvio::tmdb::TmdbSettings;

namespace {

// Minimal stub: routes /3/find/<imdb> and /3/<movie|tv>/<id>/external_ids.
class TmdbStub final : public QObject {
public:
    int hits = 0;

    bool start()
    {
        connect(&m_srv, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* sock = m_srv.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                m_buf[sock] += sock->readAll();
                if (!m_buf[sock].contains("\r\n\r\n")) return;
                ++hits;
                const QString head =
                    QString::fromUtf8(m_buf[sock]).section("\r\n", 0, 0);
                const QString path = head.section(' ', 1, 1).section('?', 0, 0);
                QByteArray body;
                if (path == "/3/find/tt123") {
                    body = R"({"movie_results":[{"id":550}],"tv_results":[]})";
                } else if (path == "/3/find/tt456") {
                    body = R"({"movie_results":[],"tv_results":[{"id":1399}]})";
                } else if (path == "/3/find/tt000") {
                    body = R"({"movie_results":[],"tv_results":[]})";
                } else if (path == "/3/movie/550/external_ids") {
                    body = R"({"imdb_id":"tt123"})";
                } else {
                    body = R"({})";
                }
                const QByteArray out =
                    QByteArray("HTTP/1.1 200 OK\r\nContent-Type: "
                               "application/json\r\nContent-Length: ") +
                    QByteArray::number(body.size()) +
                    QByteArray("\r\nConnection: close\r\n\r\n") + body;
                sock->write(out);
                sock->flush();
                sock->disconnectFromHost();
                m_buf.remove(sock);
            });
        });
        if (!m_srv.listen(QHostAddress::LocalHost, 0)) return false;
        m_port = m_srv.serverPort();
        return true;
    }

    [[nodiscard]] QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1/3").arg(m_port);
    }

private:
    QTcpServer m_srv;
    QHash<QTcpSocket*, QByteArray> m_buf;
    quint16 m_port = 0;
};

QString resolveSync(TmdbService& svc, const QString& videoId,
                    const QString& type)
{
    QString out = QStringLiteral("<pending>");
    svc.ensureTmdbId(videoId, type, [&](const QString& id) { out = id; });
    QDeadlineTimer drain(10000);
    while (!drain.hasExpired() && out == "<pending>")
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return out == "<pending>" ? QString() : out;
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
        CHECK(normalizeMediaType("movie") == "movie", "movie kept");
        CHECK(normalizeMediaType("film") == "movie", "film folds");
        CHECK(normalizeMediaType("series") == "tv", "series folds");
        CHECK(normalizeMediaType("Show") == "tv", "show folds");
        CHECK(normalizeMediaType("tvshow") == "tv", "tvshow folds");
        CHECK(normalizeMediaType("other") == "other", "unknown kept");
        CHECK(normalizeVideoId("tt123") == "tt123", "plain kept");
        CHECK(normalizeVideoId("tmdb:550") == "550", "tmdb prefix cut");
        CHECK(normalizeVideoId("series:tt123:1:2") == "tt123",
              "composite tail cut");
        CHECK(normalizeVideoId("movie:tt123/extra") == "tt123",
              "path tail cut");
        CHECK(normalizeVideoId("  tt123 ") == "tt123", "trimmed");
        CHECK(normalizeVideoId("") .isEmpty(), "empty stays empty");
        CHECK(buildTmdbUrl("https://api.themoviedb.org/3/", "/find/tt1",
                           "K", {{"external_source", "imdb_id"},
                                 {"blank", "  "}}) ==
                  "https://api.themoviedb.org/3/find/tt1?api_key=K&"
                  "external_source=imdb_id",
              "url shape, slashes trimmed, blanks dropped");
        CHECK(parseFindResult(
                  R"({"movie_results":[{"id":550}],"tv_results":[]})",
                  "movie") == "550",
              "movie pick");
        CHECK(parseFindResult(
                  R"({"movie_results":[],"tv_results":[{"id":1399}]})",
                  "tv") == "1399",
              "tv pick");
        CHECK(parseFindResult(
                  R"({"movie_results":[{"id":1}],"tv_results":[{"id":2}]})",
                  "other") == "1",
              "unknown type prefers movie");
        CHECK(parseFindResult(
                  R"({"movie_results":[],"tv_results":[]})", "movie")
                  .isEmpty(),
              "empty results resolve empty");
        CHECK(parseFindResult("garbage", "movie").isEmpty(),
              "garbage resolves empty");
        CHECK(parseExternalIds(R"({"imdb_id":"tt123"})") == "tt123",
              "external id parsed");
        CHECK(parseExternalIds(R"({})").isEmpty(), "absent id empty");
        CHECK(normalizeLanguage("en_US") == "en-US", "underscore folds");
        CHECK(normalizeLanguage("  fr  ") == "fr", "language trims");
        CHECK(normalizeLanguage("") == "", "empty language kept");
    }

    { // settings gates (fork repository semantics)
        TmdbSettings s;
        CHECK(!s.enabled() && !s.hasApiKey(), "defaults disabled/keyless");
        CHECK(s.language() == "en", "default language");
        CHECK(s.useArtwork() && !s.useReleaseDates(),
              "module defaults (artwork on, dates off)");
        s.setEnabled(true);
        CHECK(!s.enabled(), "enable without key is a no-op");
        s.setApiKey("  K1 ");
        CHECK(s.apiKey() == "K1" && s.hasApiKey(), "key trims + stores");
        s.setEnabled(true);
        CHECK(s.enabled(), "enable with key sticks");
        s.setApiKey("");
        CHECK(!s.enabled(), "clearing the key disables");
        s.setApiKey("K2");
        s.setUseArtwork(false);
        CHECK(!s.useArtwork(), "module switch flips");
        s.setProfileId(2);
        CHECK(!s.hasApiKey() && !s.enabled(),
              "profile switch reloads (new profile blank)");
        s.setProfileId(1);
        CHECK(s.apiKey() == "K2" && !s.enabled(),
              "profile 1 state intact (enabled was cleared with K1)");
        // Sync envelopes: present-only on export, per-key merge on apply.
        // (Isolated profile: profile 1 already holds a key above.)
        TmdbSettings fresh;
        fresh.setProfileId(7);
        CHECK(!fresh.exportSyncPayload().contains("tmdb_use_collections"),
              "absent keys stay absent on export");
        QJsonObject remote;
        remote.insert(QStringLiteral("tmdb_enabled"),
                      QJsonObject{{QStringLiteral("type"),
                                   QStringLiteral("boolean")},
                                  {QStringLiteral("value"), true}});
        remote.insert(QStringLiteral("tmdb_language"),
                      QJsonObject{{QStringLiteral("type"),
                                   QStringLiteral("string")},
                                  {QStringLiteral("value"), "de"}});
        CHECK(fresh.applySyncPayload(remote), "remote applies");
        CHECK(fresh.enabled() == false,
              "enabled without a key still gated after apply");
        CHECK(fresh.language() == "de", "language applied");
        CHECK(!fresh.applySyncPayload(QJsonObject{}),
              "empty payload touches nothing");
    }

    { // live mapping through the stub (caches + gates)
        TmdbStub stub;
        CHECK(stub.start(), "tmdb stub listens");
        TmdbSettings s;
        TmdbService svc(&s);
        svc.setBaseUrl(stub.baseUrl());
        CHECK(resolveSync(svc, "tt123", "movie").isEmpty(),
              "keyless resolves empty without network");
        CHECK(stub.hits == 0, "no request without a key");
        s.setApiKey("K");
        CHECK(resolveSync(svc, "tt123", "movie") == "550",
              "imdb->tmdb resolves");
        const int hitsAfterFirst = stub.hits;
        CHECK(resolveSync(svc, "tt123", "movie") == "550" &&
                  stub.hits == hitsAfterFirst,
              "mapping cached (no second request)");
        CHECK(resolveSync(svc, "550", "movie") == "550",
              "numeric ids pass through");
        CHECK(resolveSync(svc, "xyz", "movie").isEmpty(),
              "non-tt ids resolve empty");
        CHECK(resolveSync(svc, "tt000", "movie").isEmpty(),
              "unknown imdb resolves empty");
        QString back = QStringLiteral("<pending>");
        svc.tmdbToImdb(550, "movie", [&](const QString& id) { back = id; });
        QDeadlineTimer drain(10000);
        while (!drain.hasExpired() && back == "<pending>")
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(back == "tt123", "tmdb->imdb resolves");
    }

    std::printf(failures ? "TMDB SUITE FAILURES=%d\n"
                         : "TMDB SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
