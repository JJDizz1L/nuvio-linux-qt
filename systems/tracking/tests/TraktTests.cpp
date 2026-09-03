// Trakt T2 contract: scrobble body shapes, throttle/stamp rules.
#include <nuvio/tracking/TraktScrobble.h>

#include <QCoreApplication>
#include <QJsonDocument>

#include <cstdio>

#include <nuvio/tracking/TraktAuth.h>

using nuvio::tracking::TrackingEpisode;
using nuvio::tracking::TrackingMedia;
using nuvio::tracking::TrackingMediaKind;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace nuvio::tracking;

    { // movie body: title + imdb ids + progress + app version, nulls out
        TrackingMedia m;
        m.kind = TrackingMediaKind::Movie;
        m.title = "Dune";
        m.ids.imdb = "tt1160419";
        const QJsonObject body = traktScrobbleBody(m, 12.5, "0.1.20.0");
        CHECK(body.value(QStringLiteral("progress")).toDouble() == 12.5,
              "progress carried");
        CHECK(body.value(QStringLiteral("app_version")).toString() ==
                  "0.1.20.0",
              "app version carried");
        const QJsonObject movie =
            body.value(QStringLiteral("movie")).toObject();
        CHECK(movie.value(QStringLiteral("title")).toString() == "Dune",
              "movie title");
        CHECK(movie.value(QStringLiteral("ids")).toObject().value(
                  QStringLiteral("imdb")) == "tt1160419",
              "imdb id rides");
        CHECK(!movie.contains(QStringLiteral("year")),
              "unknown year omitted (never guessed)");
        CHECK(!body.contains(QStringLiteral("show")) &&
                  !body.contains(QStringLiteral("episode")),
              "movie body has no episode leg");
    }

    { // episode body: show + episode legs; progress clamps
        TrackingMedia m;
        m.kind = TrackingMediaKind::Show;
        m.title = "Show";
        m.ids.imdb = "tt123";
        m.episode.season = 2;
        m.episode.number = 4;
        m.episode.title = "Ep";
        const QJsonObject body = traktScrobbleBody(m, 150.0, "");
        CHECK(body.value(QStringLiteral("progress")).toDouble() == 100.0,
              "progress clamps to 100");
        const QJsonObject ep =
            body.value(QStringLiteral("episode")).toObject();
        CHECK(ep.value(QStringLiteral("season")).toInt() == 2 &&
                  ep.value(QStringLiteral("number")).toInt() == 4 &&
                  ep.value(QStringLiteral("title")).toString() == "Ep",
              "episode leg");
        CHECK(body.value(QStringLiteral("show")).toObject().value(
                  QStringLiteral("title")) == "Show",
              "show leg");
        CHECK(!body.contains(QStringLiteral("app_version")),
              "empty app version omitted");
        const QJsonObject low = traktScrobbleBody(m, -5.0, "v");
        CHECK(low.value(QStringLiteral("progress")).toDouble() == 0.0,
              "progress clamps to 0");
    }

    { // token expiry (60 s skew margin)
        TraktTokens empty;
        CHECK(traktTokensExpired(empty, 1000000), "no token reads expired");
        TraktTokens fresh;
        fresh.accessToken = "a";
        CHECK(!traktTokensExpired(fresh, 1000000),
              "no timestamps never expires");
        TraktTokens timed;
        timed.accessToken = "a";
        timed.createdAtSec = 1000000;
        timed.expiresInSec = 3600;
        CHECK(!traktTokensExpired(timed, 1000000 + 1000), "fresh valid");
        CHECK(traktTokensExpired(timed, 1000000 + 3600 - 59),
              "skew margin expires early");
    }

    std::printf(failures ? "TRAKT SUITE FAILURES=%d\n"
                         : "TRAKT SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
