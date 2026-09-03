// SIMKL T3 contract: scrobble body shapes, PIN poll outcomes.
#include <nuvio/tracking/SimklAuth.h>
#include <nuvio/tracking/SimklScrobble.h>

#include <QCoreApplication>

#include <cstdio>

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

    { // movie body: ids table verbatim, progress rounded to 2dp
        TrackingMedia m;
        m.kind = TrackingMediaKind::Movie;
        m.title = "Dune";
        m.ids.imdb = "tt1160419";
        m.ids.tmdb = 438631;
        const QJsonObject body = simklScrobbleBody(m, 12.3456);
        CHECK(body.value(QStringLiteral("progress")).toDouble() == 12.35,
              "progress rounds to 2dp");
        const QJsonObject movie =
            body.value(QStringLiteral("movie")).toObject();
        CHECK(movie.value(QStringLiteral("title")).toString() == "Dune",
              "movie title");
        const QJsonObject ids =
            movie.value(QStringLiteral("ids")).toObject();
        CHECK(ids.value(QStringLiteral("imdb")).toString() == "tt1160419" &&
                  ids.value(QStringLiteral("tmdb")).toInt() == 438631,
              "verbatim id table");
        CHECK(!body.contains(QStringLiteral("episode")),
              "movie body has no episode leg");
    }

    { // show + episode legs; tv-style anime rides show
        TrackingMedia m;
        m.kind = TrackingMediaKind::Show;
        m.ids.imdb = "tt123";
        m.episode.season = 2;
        m.episode.number = 4;
        const QJsonObject body = simklScrobbleBody(m, 50.0);
        CHECK(body.contains(QStringLiteral("show")) &&
                  !body.contains(QStringLiteral("anime")),
              "show leg");
        const QJsonObject ep =
            body.value(QStringLiteral("episode")).toObject();
        CHECK(ep.value(QStringLiteral("season")).toInt() == 2 &&
                  ep.value(QStringLiteral("number")).toInt() == 4,
              "episode leg with season");
        TrackingMedia a;
        a.kind = TrackingMediaKind::Anime;
        a.ids.mal = 52034;
        a.episode.number = 7;
        const QJsonObject ab = simklScrobbleBody(a, 10.0);
        CHECK(ab.contains(QStringLiteral("anime")) &&
                  !ab.contains(QStringLiteral("show")),
              "season-less anime rides anime leg");
        a.episode.season = 1;
        const QJsonObject ab2 = simklScrobbleBody(a, 10.0);
        CHECK(ab2.contains(QStringLiteral("show")) &&
                  !ab2.contains(QStringLiteral("anime")),
              "seasoned anime rides show leg");
    }

    { // PIN poll outcomes (verbatim result/device_code/result-KO shapes)
        QString token;
        CHECK(simklPinPollOutcome(
                  QByteArray("{\"result\":\"OK\","
                             "\"access_token\":\"tok\"}"),
                  &token) == SimklPinOutcome::Authorized &&
                  token == "tok",
              "OK + token authorizes");
        CHECK(simklPinPollOutcome(
                  QByteArray("{\"result\":\"KO\"}"),
                  nullptr) == SimklPinOutcome::Pending,
              "KO pends");
        CHECK(simklPinPollOutcome(
                  QByteArray("{\"device_code\":\"abc\"}"),
                  nullptr) == SimklPinOutcome::Gone,
              "device_code present means gone");
        CHECK(simklPinPollOutcome(QByteArray("{}"), nullptr) ==
                  SimklPinOutcome::Failed,
              "empty fails");
        CHECK(simklPinPollOutcome(QByteArray("garbage"), nullptr) ==
                  SimklPinOutcome::Failed,
              "garbage fails");
    }

    std::printf(failures ? "SIMKL SUITE FAILURES=%d\n"
                         : "SIMKL SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
