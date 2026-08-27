// Offline contract for Cinemeta detail-page normalization. Fixtures are the
// real wire shapes (modern AND legacy video-id forms); no network here.
#include <nuvio/library/MetaService.h>

#include <QCoreApplication>
#include <cstdio>

using nuvio::library::MetaService;
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

    const QByteArray body =
        "{\n"
        "  \"meta\": {\n"
        "    \"id\": \"tt0944947\",\n"
        "    \"type\": \"series\",\n"
        "    \"name\": \"Game of Thrones\",\n"
        "    \"description\": \"Winter is coming.\",\n"
        "    \"releaseInfo\": \"2011-2019\",\n"
        "    \"runtime\": \"57 min\",\n"
        "    \"imdbRating\": \"9.2\",\n"
        "    \"genres\": [\"Fantasy\", \"Drama\"],\n"
        "    \"cast\": [\"Peter Dinklage\", {\"name\": \"Emilia Clarke\"}],\n"
        "    \"videos\": [\n"
        "      {\"id\": \"tt0944947:1:2\", \"name\": \"The Kingsroad\",\n"
        "       \"season\": -1, \"overview\": \"ep two\", \"thumbnail\": "
        "\"https://img/12.jpg\"},\n"
        "      {\"id\": \"tt0944947::season:1:episode:10\", \"name\": \"Fire and"
        " Blood\"},\n"
        "      {\"id\": \"tt0944947:1:1\", \"name\": \"Winter Is Coming\",\n"
        "       \"thumbnail\": \"https://img/01.jpg\"},\n"
        "      {\"id\": \"garbage-no-episode\", \"name\": \"dropped\"}\n"
        "    ]\n"
        "  }\n"
        "}";

    const QVariantMap m = MetaService::metaFromJson(body);
    CHECK(!m.isEmpty(), "well-formed body parses");
    CHECK(m.value("id").toString() == "tt0944947", "id passthrough");
    CHECK(m.value("name").toString() == "Game of Thrones", "name passthrough");
    CHECK(m.value("imdbRating").toString() == "9.2", "rating passthrough");

    const QStringList cast = m.value("cast").toStringList();
    CHECK(cast.size() == 2 && cast[1] == "Emilia Clarke",
          "cast handles strings and {name} objects");

    // Episode ordering + both id shapes parsed (season/episode fields were
    // deliberately poisoned on the modern entry to force id parsing).
    const QVariantList eps = m.value("videos").toList();
    CHECK(eps.size() == 3, "malformed video dropped, 3 kept");
    if (eps.size() == 3) {
        const auto& e1 = eps[0].toMap();
        CHECK(e1["season"].toInt() == 1 && e1["episode"].toInt() == 1,
              "modern id tt:S:E parsed (sorted first)");
        CHECK(e1["thumb"].toString() == "https://img/01.jpg",
              "thumbnail passthrough");
        const auto& e2 = eps[1].toMap();
        CHECK(e2["season"].toInt() == 1 && e2["episode"].toInt() == 2
                  && e2["description"].toString() == "ep two",
              "second episode sorted between");
        const auto& e3 = eps[2].toMap();
        CHECK(e3["season"].toInt() == 1 && e3["episode"].toInt() == 10,
              "legacy ::season:X:episode:Y id parsed; numeric sort not "
              "string sort ('10' after '2')");
    }

    CHECK(MetaService::metaFromJson("{\"meta\":{\"id\":\"nm0001\"}}")
              .isEmpty(),
          "non-tt ids rejected (imdb-only parity)");
    CHECK(MetaService::metaFromJson("not json at all").isEmpty(),
          "malformed body -> empty map, never partial model");

    std::printf(failures ? "META SUITE FAILURES=%d\n"
                         : "META SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
