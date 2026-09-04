// Offline contract for MDBList settings + ratings: key semantics,
// gates, envelopes, URL/body/parse/format rules, and live provider
// fan-out through a local HTTP stub (XDG-sandboxed; api.mdblist.com
// never touched).
#include <nuvio/mdblist/MdbListService.h>
#include <nuvio/mdblist/MdbListSettings.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QHostAddress>
#include <QJsonObject>
#include <QRegularExpression>
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

using nuvio::mdblist::extractImdbId;
using nuvio::mdblist::formatOneDecimal;
using nuvio::mdblist::formatPercent;
using nuvio::mdblist::formatWhole;
using nuvio::mdblist::kProviderPriority;
using nuvio::mdblist::kRatingDisplayOrder;
using nuvio::mdblist::MdbListService;
using nuvio::mdblist::MdbListSettings;
using nuvio::mdblist::parseRating;
using nuvio::mdblist::ratingRequestBody;
using nuvio::mdblist::ratingUrl;
using nuvio::mdblist::toMdbListMediaType;

namespace {

class MdbStub final : public QObject {
public:
    int hits = 0;
    QString lastMethod;
    QByteArray lastBody;

    bool start()
    {
        connect(&m_srv, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* sock = m_srv.nextPendingConnection();
            connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                m_buf[sock] += sock->readAll();
                const int split = m_buf[sock].indexOf("\r\n\r\n");
                if (split < 0) return;
                // Wait for the full POST body before answering (early
                // close resets in-flight uploads on some stacks).
                const QString headers =
                    QString::fromUtf8(m_buf[sock].left(split));
                int contentLength = 0;
                const QRegularExpression cl(
                    QStringLiteral("Content-Length:\\s*(\\d+)"));
                if (const auto m = cl.match(headers); m.hasMatch())
                    contentLength = m.captured(1).toInt();
                if (m_buf[sock].size() < split + 4 + contentLength) return;
                const QByteArray raw = m_buf[sock];
                m_buf.remove(sock);
                ++hits;
                const QString head =
                    QString::fromUtf8(raw.left(split)).section("\r\n", 0, 0);
                lastMethod = head.section(' ', 0, 0);
                const QString path =
                    head.section(' ', 1, 1).section('?', 0, 0);
                lastBody = raw.mid(split + 4);
                const QString provider = path.section('/', 3, 3);
                // Distinct values per provider prove the mapping.
                const double value = provider == "imdb"      ? 8.5
                                     : provider == "tomatoes" ? 92.0
                                                              : 7.0;
                const QByteArray body = QByteArray("{\"ratings\":[{\"rating\":") +
                                        QByteArray::number(value) + "}]}";
                const QByteArray out =
                    QByteArray("HTTP/1.1 200 OK\r\nContent-Type: "
                               "application/json\r\nContent-Length: ") +
                    QByteArray::number(body.size()) +
                    QByteArray("\r\nConnection: close\r\n\r\n") + body;
                const qint64 written = sock->write(out);
                Q_UNUSED(written);
                sock->flush();
                sock->disconnectFromHost();
            });
        });
        if (!m_srv.listen(QHostAddress::LocalHost, 0)) return false;
        m_port = m_srv.serverPort();
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

QVariantList resolveSync(MdbListService& svc, const QString& type,
                         const QString& id)
{
    QVariantList out;
    bool done = false;
    svc.ratingsFor(type, id, [&](const QVariantList& rows) {
        out = rows;
        done = true;
    });
    QDeadlineTimer drain(15000);
    while (!drain.hasExpired() && !done)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return out;
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
        CHECK(extractImdbId("tt123") == "tt123", "plain imdb");
        CHECK(extractImdbId("tt123:1:2") == "tt123", "composite head");
        CHECK(extractImdbId("kitsu:48899") == "", "non-imdb empty");
        CHECK(extractImdbId("") == "", "empty empty");
        CHECK(toMdbListMediaType("movie") == "movie", "movie stays");
        CHECK(toMdbListMediaType("series") == "show", "series folds");
        CHECK(toMdbListMediaType(" anything ") == "show", "default show");
        CHECK(ratingUrl("https://api.mdblist.com/", "movie", "imdb",
                        "K") ==
                  "https://api.mdblist.com/rating/movie/imdb?apikey=K",
              "rating url shape");
        CHECK(QString::fromUtf8(ratingRequestBody("tt123")) ==
                  "{\"ids\":[\"tt123\"],\"provider\":\"imdb\"}",
              "request body shape");
        CHECK(parseRating("{\"ratings\":[{\"rating\":8.5}]}")
                      .value_or(-1.0) == 8.5,
              "rating parsed");
        CHECK(!parseRating("{\"ratings\":[]}").has_value(),
              "empty ratings absent");
        CHECK(!parseRating("{\"ratings\":[{}]}").has_value(),
              "null rating absent");
        CHECK(!parseRating("garbage").has_value(), "garbage absent");
        CHECK(formatOneDecimal(8.55) == "8.6", "one-decimal rounds");
        CHECK(formatOneDecimal(8.0) == "8.0", "one-decimal pads");
        CHECK(formatWhole(7.5) == "8", "whole rounds half away");
        CHECK(formatPercent(92.0) == "92%", "percent shapes");
        CHECK(kProviderPriority.size() == 8 &&
                  kProviderPriority.first() == "imdb" &&
                  kProviderPriority.last() == "mal",
              "fetch priority order");
        CHECK(kRatingDisplayOrder.size() == 8 &&
                  kRatingDisplayOrder.first().source == "imdb" &&
                  kRatingDisplayOrder.last().source == "metacritic",
              "display order");
    }

    { // settings gates (fork repository semantics)
        MdbListSettings s;
        CHECK(!s.enabled() && !s.hasApiKey(), "defaults disabled/keyless");
        CHECK(s.isProviderEnabled("imdb") && !s.isProviderEnabled("nope"),
              "provider gate (unknown never enabled)");
        CHECK(s.enabledProviders().size() == 8, "all providers by default");
        s.setEnabled(true);
        CHECK(!s.enabled(), "enable without key is a no-op");
        s.setApiKey("  K1 ");
        CHECK(s.apiKey() == "K1" && s.hasApiKey(), "key trims + stores");
        s.setEnabled(true);
        CHECK(s.enabled(), "enable with key sticks");
        s.setProviderEnabled("bogus", false);
        CHECK(s.enabledProviders().size() == 8, "unknown id ignored");
        s.setProviderEnabled("tomatoes", false);
        CHECK(!s.isProviderEnabled("TOMATOES") &&
                  s.enabledProviders().size() == 7,
              "provider toggle flips (case-insensitive)");
        s.setApiKey("");
        CHECK(!s.enabled(), "clearing the key disables");
        s.setApiKey("K1");
        s.setEnabled(true);
        s.setProfileId(2);
        CHECK(!s.hasApiKey() && !s.enabled(),
              "profile switch reloads (new profile blank)");
        s.setProfileId(1);
        CHECK(s.apiKey() == "K1" && !s.isProviderEnabled("tomatoes"),
              "profile 1 state intact");
        // Sync envelopes: present-only on export, per-key merge on apply.
        MdbListSettings fresh;
        fresh.setProfileId(7);
        CHECK(!fresh.exportSyncPayload().contains("mdblist_use_mal"),
              "absent keys stay absent on export");
        QJsonObject remote;
        remote.insert(QStringLiteral("mdblist_enabled"),
                      QJsonObject{{QStringLiteral("type"),
                                   QStringLiteral("boolean")},
                                  {QStringLiteral("value"), true}});
        remote.insert(QStringLiteral("mdblist_use_mal"),
                      QJsonObject{{QStringLiteral("type"),
                                   QStringLiteral("boolean")},
                                  {QStringLiteral("value"), false}});
        CHECK(fresh.applySyncPayload(remote), "remote applies");
        CHECK(!fresh.enabled(), "enabled without a key still gated");
        CHECK(!fresh.isProviderEnabled("mal"), "provider applied");
        CHECK(!fresh.applySyncPayload(QJsonObject{}),
              "empty payload touches nothing");
    }

    { // live fan-out through the stub (gates + cache + mapping)
        MdbStub stub;
        CHECK(stub.start(), "mdblist stub listens");
        MdbListSettings s;
        s.setProfileId(3);   // isolated: profile 1 is dirty from above
        MdbListService svc(&s);
        svc.setBaseUrl(stub.baseUrl());
        CHECK(resolveSync(svc, "movie", "tt123").isEmpty(),
              "keyless resolves empty without network");
        CHECK(stub.hits == 0, "no request without a key");
        s.setApiKey("K");
        s.setEnabled(true);
        const QVariantList rows = resolveSync(svc, "movie", "tt123");
        CHECK(stub.hits == 8, "one POST per provider");
        CHECK(stub.lastMethod == "POST", "ratings ride POST");
        CHECK(rows.size() == 8, "all providers mapped");
        CHECK(rows[0].toMap().value("source").toString() == "imdb",
              "priority order kept");
        const int hitsAfterFirst = stub.hits;
        CHECK(resolveSync(svc, "movie", "tt123").size() == 8 &&
                  stub.hits == hitsAfterFirst,
              "mapping cached (no second wave)");
        s.setProviderEnabled("mal", false);
        resolveSync(svc, "movie", "tt123");
        CHECK(stub.hits == hitsAfterFirst + 7,
              "provider set change re-fetches (cache cleared)");
        CHECK(resolveSync(svc, "movie", "kitsu:1").isEmpty(),
              "non-imdb ids resolve empty");
    }

    std::printf(failures ? "MDBLIST SUITE FAILURES=%d\n"
                         : "MDBLIST SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
