// Offline parse/mapping contract for Cinemeta catalog entries. Network leg
// is covered by the live run (NUVIO_CINEMETA_BASE + manual verification);
// everything here is deterministic.
#include <nuvio/library/CatalogService.h>

#include <cstdio>

using nuvio::library::CatalogService;
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
    { // full metahub poster passthrough
        const auto item = CatalogService::itemFromMeta(
            QVariantMap{{"id", "tt0111161"},
                        {"name", "The Shawshank Redemption"},
                        {"releaseInfo", "1994"},
                        {"poster",
                         "https://images.metahub.space/poster/medium/"
                         "tt0111161/img"}},
            "https://v3-cinemeta.strem.io");
        CHECK(item.value("poster").toString() ==
                  "https://images.metahub.space/poster/medium/tt0111161/img",
              "absolute poster kept");
        CHECK(item.value("name") == "The Shawshank Redemption", "name kept");
        CHECK(item.value("year") == "1994", "year from releaseInfo");
    }
    { // missing poster -> canonical metahub build from imdb id
        const auto item = CatalogService::itemFromMeta(
            QVariantMap{{"id", "tt0133093"}, {"name", "The Matrix"}},
            "https://v3-cinemeta.strem.io");
        CHECK(item.value("poster").toString() ==
                  "https://v3-cinemeta.strem.io/tt0133093/img",
              "fallback poster built");
    }
    { // non-imdb ids are dropped (skeleton scope: movies via addon)
        const auto item = CatalogService::itemFromMeta(
            QVariantMap{{"id", "yt:abc123"}, {"name", "Trailer"}},
            "https://x");
        CHECK(item.isEmpty(), "non-imdb dropped");
    }

    std::printf(failures ? "CATALOG SUITE FAILURES=%d\n"
                         : "CATALOG SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}