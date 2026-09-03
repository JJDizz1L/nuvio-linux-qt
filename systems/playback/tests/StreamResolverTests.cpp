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

    { // bestTorrent exposes p2p-routable entries; direct still wins tier 1
        r.setAddons(QVariantList{
            QVariantMap{{"id", "torrentio"}, {"name", "Torrentio"},
                        {"url", "https://torrentio.strem.fun/manifest.json"}},
            QVariantMap{{"id", "direct"}, {"name", "DirectAdd"},
                        {"url",
                         "https://example.com/strem/manifest.json"}},
        });
        // key tt0111161 was ingested above with a torrent entry (torrentio)
        // AND a direct entry (direct): tier 1 unaffected...
        const auto best = r.bestFor("movie", "tt0111161");
        CHECK(best.value("source").toString() == "torrentio",
              "tier 1 unchanged by torrent retention");
        const auto tor = r.bestTorrent("movie", "tt0111161");
        CHECK(tor.value("infoHash").toString() == "d5a4b0e7a1c93f88",
              "bestTorrent returns first infoHash in addon order");
        // all-torrent case: no direct anywhere, but routable (fresh key so
        // the reconfigured addon set answers it completely)
        const QByteArray onlyTorrents =
            R"({"streams":[{"title":"t","infoHash":"abcd"}]})";
        r.applyAddonStreams("movie/tt8888888", "torrentio", onlyTorrents);
        r.applyAddonStreams("movie/tt8888888", "direct", emptyBody);
        const auto tor2 = r.bestTorrent("movie", "tt8888888");
        CHECK(tor2.value("infoHash").toString() == "abcd",
              "all-torrent case exposes hash instead of nothing");
        // series/tt0903747 ingested earlier had NO torrent entries
        CHECK(r.bestTorrent("series", "tt0903747").isEmpty(),
              "no torrents stored -> empty map");
    }

    { // P3d: allStreams listing + addon display names for the scope panel
        const auto rows = r.allStreams("movie", "tt0111161");
        CHECK(rows.size() == 3, "torrent + direct + direct rows listed");
        CHECK(rows[0].toMap().value("source").toString() == "torrentio",
              "addon order kept");
        CHECK(rows[0].toMap().value("sourceName").toString() == "Torrentio",
              "display name resolved");
        CHECK(rows[0].toMap().value("torrent").toBool() == true,
              "torrent row flagged");
        CHECK(rows[1].toMap().value("torrent").toBool() == false,
              "direct row not flagged");
        CHECK(rows[2].toMap().value("sourceName").toString() == "DirectAdd",
              "second addon named");
        CHECK(r.addonName("torrentio") == "Torrentio", "name lookup");
        CHECK(r.addonName("nope") == "", "unknown id -> empty");
        CHECK(r.allStreams("movie", "unseen").isEmpty(),
              "unresolved key lists nothing");
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