// NextEpisodeRules contract: ordered continuation, threshold/outro card
// visibility with Compose clamps, aired compare, id helpers.
#include <nuvio/playback/NextEpisodeRules.h>

#include <cstdio>
#include <tuple>

using nuvio::playback::CompositeId;
using nuvio::playback::EpisodeRef;
using nuvio::playback::SkipSegment;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {
QList<EpisodeRef> eps(std::initializer_list<std::tuple<int, int, QString>> xs)
{
    QList<EpisodeRef> out;
    for (const auto& [s, e, id] : xs) {
        EpisodeRef r;
        r.season = s;
        r.episode = e;
        r.id = id;
        out.append(r);
    }
    return out;
}
} // namespace

int main()
{
    using namespace nuvio::playback;

    { // ordered continuation incl. specials-first sorting + boundaries
        const auto vids = eps({{0, 1, "a"}, {1, 2, "b"}, {1, 1, "c"}});
        const auto n1 = resolveNextEpisode(vids, 1, 1);
        CHECK(n1 && n1->id == "b", "next after S1E1 is S1E2 (sorted)");
        const auto n0 = resolveNextEpisode(vids, 0, 1);
        CHECK(n0 && n0->id == "c", "specials precede season 1");
        CHECK(!resolveNextEpisode(vids, 1, 2), "last episode has no next");
        CHECK(!resolveNextEpisode(vids, 9, 9), "unknown episode has no next");
        CHECK(!resolveNextEpisode(vids, -1, -1), "movies never continue");
    }

    { // threshold card (no intervals): percent clamp 97..100
        CHECK(shouldShowNextEpisodeCard(9900, 10000, {}, "PERCENTAGE", 99, 2),
              "99% fires at 99%");
        CHECK(!shouldShowNextEpisodeCard(5000, 10000, {}, "PERCENTAGE", 99, 2),
              "99% silent at 50%");
        // Clamp: 50% request behaves as 97%.
        CHECK(shouldShowNextEpisodeCard(9700, 10000, {}, "PERCENTAGE", 50, 2),
              "sub-97 percent clamps to 97");
        CHECK(!shouldShowNextEpisodeCard(9600, 10000, {}, "PERCENTAGE", 50, 2),
              "clamp boundary respected");
        // Minutes clamp 0..3.5, remaining-based.
        CHECK(shouldShowNextEpisodeCard(590000, 600000, {},
                                        "MINUTES_BEFORE_END", 99, 2),
              "2min window fires with 10s left");
        CHECK(!shouldShowNextEpisodeCard(400000, 600000, {},
                                         "MINUTES_BEFORE_END", 99, 2),
              "2min window silent earlier");
        CHECK(!shouldShowNextEpisodeCard(1000, 0, {}, "PERCENTAGE", 99, 2),
              "zero duration never fires");
    }

    { // outro-aware: ends-near-file-end fires at earliest outro start
        const QList<SkipSegment> outros{
            {1700.0, 1790.0, "outro"},
        };
        // 30h media? No: 1800s file, outro ends 10s before end, threshold
        // 2min(120s) > gap(10s) -> early-fire at outro start (1700s).
        CHECK(shouldShowNextEpisodeCard(1700000, 1800000, outros,
                                        "MINUTES_BEFORE_END", 99, 2),
              "early-fire at outro start");
        CHECK(!shouldShowNextEpisodeCard(1600000, 1800000, outros,
                                         "MINUTES_BEFORE_END", 99, 2),
              "silent before outro start");
        // Outro ends far from file end -> threshold rule instead.
        const QList<SkipSegment> midOutros{
            {500.0, 560.0, "ed"},
        };
        CHECK(!shouldShowNextEpisodeCard(600000, 1800000, midOutros,
                                         "MINUTES_BEFORE_END", 99, 2),
              "mid-file outro defers to threshold");
        CHECK(shouldShowNextEpisodeCard(1700000, 1800000, midOutros,
                                        "MINUTES_BEFORE_END", 99, 2),
              "threshold still fires late");
        // Non-outro segments never trigger the outro path.
        const QList<SkipSegment> intros{{60.0, 150.0, "intro"}};
        CHECK(!shouldShowNextEpisodeCard(100000, 1800000, intros,
                                         "PERCENTAGE", 99, 2),
              "intro segments ignored by card rule");
    }

    { // aired compare + id helpers
        CHECK(hasAired("", 2026, 9, 3), "unknown counts as aired");
        CHECK(hasAired("2020-01-01", 2026, 9, 3), "past aired");
        CHECK(!hasAired("2030-05-05", 2026, 9, 3), "future unaired");
        CHECK(hasAired("2026-09-03", 2026, 9, 3), "today aired");
        CHECK(hasAired("garbage", 2026, 9, 3), "garbage counts as aired");
        const auto imdb = extractImdbId("kitsu:48899 stuff tt12345 tail");
        CHECK(imdb && *imdb == "tt12345", "tt token extracted");
        CHECK(!extractImdbId("kitsu:48899"), "no tt token");
        const CompositeId ep = splitCompositeId("tt123:2:4");
        CHECK(ep.isEpisode() && ep.parent == "tt123" && ep.season == 2 &&
                  ep.episode == 4,
              "composite splits");
        const CompositeId mv = splitCompositeId("tt123");
        CHECK(!mv.isEpisode() && mv.parent == "tt123", "movie unsplit");
    }

    std::printf(failures ? "NEXTEP SUITE FAILURES=%d\n"
                         : "NEXTEP SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
