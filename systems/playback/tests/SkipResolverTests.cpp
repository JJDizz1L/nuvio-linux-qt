// SkipResolver contract: provider urls, tolerant parses, priority merge,
// completion keys.
#include <nuvio/playback/SkipResolver.h>

#include <cstdio>

using nuvio::playback::SkipSegment;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

int main()
{
    using namespace nuvio::playback;

    { // provider urls (verbatim Compose shapes)
        CHECK(introDbSegmentsUrl("", "tt1", 1, 2) == "",
              "blank base disables IntroDb (Compose default)");
        CHECK(introDbSegmentsUrl("https://introdb.app/", "tt123", 2, 4) ==
                  "https://introdb.app/segments?imdb_id=tt123&season=2&episode=4",
              "introdb segments url (trailing slash trimmed)");
        CHECK(aniskipUrl("52034", 7) ==
                  "https://api.aniskip.com/v2/skip-times/52034/7"
                  "?types=op&types=ed&types=recap&types=mixed-op"
                  "&types=mixed-ed&episodeLength=0",
              "aniskip url");
        CHECK(introDbSubmitUrl("https://introdb.app") ==
                  "https://introdb.app/submit",
              "submit url");
        CHECK(introDbSubmitUrl("") == "", "blank base disables submit");
    }

    { // IntroDb parse: sec||ms legs, end<=start dropped, provider tagged
        const QByteArray body = R"({"intro":{"start_sec":63.0,"end_sec":150.5},
            "recap":{"start_ms":900000,"end_ms":960000},
            "outro":{"start_sec":1300.0,"end_sec":1200.0}})";
        const auto segs = parseIntroDbSegments(body);
        CHECK(segs.size() == 2, "intro+recap kept, bad outro dropped");
        CHECK(segs[0].type == "intro" && segs[0].provider == "introdb" &&
                  segs[0].startSec == 63.0 && segs[0].endSec == 150.5,
              "intro seconds leg");
        CHECK(segs[1].type == "recap" && segs[1].startSec == 900.0 &&
                  segs[1].endSec == 960.0,
              "recap millis leg");
        CHECK(parseIntroDbSegments("garbage").isEmpty(), "garbage -> empty");
    }

    { // AniSkip parse: found gate, results mapping
        const QByteArray body = R"({"found":true,"results":[
            {"skipType":"op","interval":{"startTime":12.0,"endTime":102.0}},
            {"skipType":"ed","interval":{"startTime":1400.0,"endTime":720.0}}]})";
        const auto segs = parseAniSkipTimes(body);
        CHECK(segs.size() == 1, "bad ed interval dropped");
        CHECK(segs[0].type == "op" && segs[0].provider == "aniskip" &&
                  segs[0].startSec == 12.0,
              "op mapped");
        CHECK(parseAniSkipTimes(R"({"found":false})").isEmpty(),
              "unfound -> empty");
    }

    { // priority merge: first provider wins per category
        const QList<SkipSegment> introdb{
            {60.0, 150.0, "intro", "introdb"},
            {1400.0, 1500.0, "outro", "introdb"},
        };
        const QList<SkipSegment> aniskip{
            {55.0, 145.0, "op", "aniskip"},
            {800.0, 860.0, "recap", "aniskip"},
            {10.0, 20.0, "mystery", "aniskip"},
        };
        const auto merged = mergeSkipIntervals({introdb, aniskip});
        CHECK(merged.size() == 3, "opening+ending+recap, mystery dropped");
        CHECK(merged[0].provider == "introdb" && merged[0].type == "intro",
              "introdb opening wins by order");
        CHECK(merged[1].provider == "introdb", "introdb ending wins");
        CHECK(merged[2].type == "recap" && merged[2].provider == "aniskip",
              "recap filled from aniskip");
        CHECK(skipCompletionKey("introdb", "intro", 60.0, 150.0) ==
                  "introdb:intro:60:150",
              "completion key shape");
    }

    std::printf(failures ? "SKIP SUITE FAILURES=%d\n"
                         : "SKIP SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
