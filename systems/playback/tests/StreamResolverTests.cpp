// Offline contract for stream selection policy. Bodies are fixture
// fixtures straight from the Stremio addon wire format.
#include <nuvio/playback/StreamResolver.h>

#include <QCoreApplication>
#include <cstdio>

using nuvio::playback::StreamResolver;
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
    QCoreApplication app(argc, argv);   // event-loop signals for completeness

    StreamResolver r;
    r.setAddons(QVariantList{
        QVariantMap{{"id", "torrentio"}, {"name", "Torrentio"},
                    {"url", "https://torrentio.strem.fun/manifest.json"}},
        QVariantMap{{"id", "direct"}, {"name", "DirectAdd"},
                    {"url",
                     "https://example.com/strem/manifest.json"}},
    });
    CHECK(r.addonIds().size() == 2, "two addons configured");

    const QByteArray torrentBody =
        R"({"streams":[)"
        R"({"title":"4K HDR","infoHash":"d5a4b0e7a1c93f88"},)"
        R"({"title":"1080p","url":"https://cdn.example/movie.mkv"}]})";
    const QByteArray directBody =
        R"({"streams":[{"title":"Direct 1080p",)"
        R"("externalUrl":"https://dl.example/play.m3u8"}]})";
    const QByteArray emptyBody = R"({"streams":[]})";

    { // first-arriving addon alone -> best is its best direct entry
        r.applyAddonStreams("movie/tt0111161", "torrentio", torrentBody);
        const auto best1 = r.bestFor("movie", "tt0111161");
        CHECK(best1.value("playable").toBool(), "direct entry picked");
        CHECK(best1.value("url").toString() ==
                  "https://cdn.example/movie.mkv",
              "url entry over infoHash");
    }
    { // second addon answers; earlier addon wins by user order
        int updates = 0;
        QObject con;
        QObject::connect(&r, &StreamResolver::streamsUpdated, &con,
                         [&updates](QString, QString) { ++updates; });
        r.applyAddonStreams("movie/tt0111161", "direct", directBody);
        CHECK(updates >= 1, "update signaled");
        const auto best2 = r.bestFor("movie", "tt0111161");
        // torrentio is FIRST in the list: its cdn url still wins
        CHECK(best2.value("source").toString() == "torrentio",
              "addon order decides");
    }
    { // empty body counts as answered, yields no streams from that addon
        r.applyAddonStreams("series/tt0903747", "torrentio", emptyBody);
        r.applyAddonStreams("series/tt0903747", "direct", directBody);
        const auto s = r.bestFor("series", "tt0903747");
        CHECK(s.value("source").toString() == "direct",
              "other addon fills in");
    }
    { // all-torrent case: nothing directly playable -> empty best (honest)
        const QByteArray onlyTorrents =
            R"({"streams":[{"title":"t","infoHash":"abcd"}]})";
        r.setAddons(QVariantList{QVariantMap{
            {"id", "onlytor"}, {"name", "OnlyT"},
            {"url", "https://x.example/manifest.json"}}});
        r.applyAddonStreams("movie/tt9999999", "onlytor", onlyTorrents);
        CHECK(r.bestFor("movie", "tt9999999").isEmpty(),
              "torrent-only stays unplayable (no phantom states)");
    }

    { // isComplete mirrors per-key completeness only
        StreamResolver solo;
        solo.setAddons(QVariantList{QVariantMap{
            {"id", "solo"}, {"name", "Solo"},
            {"url", "https://s.example/manifest.json"}}});
        CHECK(!solo.isComplete("movie", "tt1"), "nothing answered yet");
        solo.applyAddonStreams("movie/tt1", "solo", emptyBody);
        CHECK(solo.isComplete("movie", "tt1"), "answered == expected");
        CHECK(!solo.isComplete("movie", "tt2"), "other key unaffected");

        StreamResolver none;                    // zero addons configured
        CHECK(none.isComplete("movie", "anyid"),
              "zero addons trivially complete (honest empty-best)");
    }

    std::printf(failures ? "STREAM SUITE FAILURES=%d\n"
                         : "STREAM SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}