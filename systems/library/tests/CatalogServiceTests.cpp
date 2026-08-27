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

    { // multi-shelf container: order stability + ingest folding
        CatalogService svc;
        CHECK(svc.shelves().isEmpty(), "starts with no shelves");

        const QByteArray movieJson =
            R"({"metas":[{"id":"tt0111161","name":"Shawshank",)"
            R"("releaseInfo":"1994","poster":""},)"
            R"({"id":"tt0133093","name":"Matrix"}]})";
        const QByteArray seriesJson =
            R"({"metas":[{"id":"tt0903747","name":"Breaking Bad"}]})";

        svc.ingest("movie", "top", movieJson);
        svc.ingest("series", "top", seriesJson);
        svc.ingest("anime", "top",
                   QByteArrayLiteral(R"({"metas":[]})"));   // legitimately empty

        const auto shelves = svc.shelves();
        CHECK(shelves.size() == 3, "three shelves in request order");
        CHECK(shelves[0].toMap().value("title") == "Popular Movies",
              "rail 1 title");
        CHECK(shelves[1].toMap().value("title") == "Popular Series",
              "rail 2 title");
        CHECK(shelves[2].toMap().value("items").toList().isEmpty(),
              "empty rail is still a rail");
        for (const auto& s : shelves)
            CHECK(!s.toMap().value("loading").toBool(),
                  "ingest clears loading flag");

        // re-ingest same category REPLACES items, never duplicates the rail
        svc.ingest("movie", "top",
                   QByteArrayLiteral(R"({"metas":[{"id":"tt0816692",)"
                                     R"("name":"Interstellar"}]})"));
        CHECK(svc.shelves().size() == 3, "no duplicate rails on re-ingest");
        CHECK(svc.shelves()[0].toMap().value("items")
                  .toList().size() == 1,
              "re-ingest replaces items");

        // malformed bodies fold as EMPTY (loading cleared), not exceptions
        svc.ingest("anime", "top", QByteArrayLiteral("{not json"));
        CHECK(svc.shelves()[2].toMap().value("items").toList().isEmpty() &&
              !svc.shelves()[2].toMap().value("loading").toBool(),
              "bad body degrades to empty-not-stuck");
    }

    std::printf(failures ? "CATALOG SUITE FAILURES=%d\n"
                         : "CATALOG SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}