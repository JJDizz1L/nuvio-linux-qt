// OFFLINE deeplink tests (parse/build kernel: all eight upstream cases
// plus builders, scheme filter, and path/query variants). No isolation
// needs (pure).
#include <nuvio/deeplink/DeepLink.h>

#include <QCoreApplication>

#include <cstdio>

using nuvio::deeplink::DeepLink;
using nuvio::deeplink::DeepLinkKind;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {

bool isMeta(const DeepLink& link, const char* type, const char* id)
{
    return link.kind == DeepLinkKind::Meta &&
           link.meta.type == QLatin1String(type) &&
           link.meta.id == QLatin1String(id);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using nuvio::deeplink::buildDownloadsUrl;
    using nuvio::deeplink::buildMetaUrl;
    using nuvio::deeplink::isAppUrl;
    using nuvio::deeplink::parseDeepLink;

    // The eight upstream cases, verbatim.
    CHECK(isMeta(parseDeepLink("nuvio://meta?type=series&id=tt0944947"),
                 "series", "tt0944947"),
          "notification meta deeplink");
    DeepLink addon =
        parseDeepLink("nuvio://free.nebulapro.xyz/sports/i/free/manifest.json");
    CHECK(addon.kind == DeepLinkKind::AddonInstall &&
              addon.addon.manifestUrl ==
                  "https://free.nebulapro.xyz/sports/i/free/manifest.json",
          "nuvio addon install");
    addon = parseDeepLink(
        "stremio://free.nebulapro.xyz/sports/i/free/manifest.json");
    CHECK(addon.kind == DeepLinkKind::AddonInstall &&
              addon.addon.manifestUrl ==
                  "https://free.nebulapro.xyz/sports/i/free/manifest.json",
          "stremio addon install");
    CHECK(isMeta(parseDeepLink("nuvio://series/tt0944947"), "series",
                 "tt0944947"),
          "direct series detail");
    CHECK(isMeta(parseDeepLink("nuvio://imdb/series/tt0944947"), "series",
                 "tt0944947"),
          "provider imdb detail");
    CHECK(isMeta(parseDeepLink("nuvio://tmdb/tv/1399"), "series",
                 "tmdb:1399"),
          "provider tmdb detail");
    CHECK(parseDeepLink("nuvio://auth/trakt?code=abc").kind ==
              DeepLinkKind::None,
          "auth reserved");
    CHECK(parseDeepLink("stremio://detail/series/tt0944947").kind ==
              DeepLinkKind::None,
          "stremio non-host");

    // Builders round-trip through the parser.
    CHECK(isMeta(parseDeepLink(buildMetaUrl("movie", "tt0111161")), "movie",
                 "tt0111161"),
          "meta builder round-trip");
    CHECK(parseDeepLink(buildDownloadsUrl()).kind ==
              DeepLinkKind::Downloads,
          "downloads builder round-trip");
    CHECK(buildMetaUrl(" series ", "tt1").contains("type=series&id=tt1"),
          "builder trims");

    // Scheme filter + hygiene.
    CHECK(isAppUrl("NUVIO://meta?type=movie&id=tt1"), "case-insensitive");
    CHECK(isAppUrl("  stremio://example.com/manifest.json  "), "trimmed");
    CHECK(!isAppUrl("https://example.com/meta"), "https rejected");
    CHECK(!isAppUrl(""), "blank rejected");
    CHECK(parseDeepLink("").kind == DeepLinkKind::None, "blank parses none");
    CHECK(parseDeepLink("https://example.com").kind == DeepLinkKind::None,
          "foreign scheme none");

    // Path/query variants.
    CHECK(isMeta(parseDeepLink("nuvio://detail/movie/tt0111161"), "movie",
                 "tt0111161"),
          "detail path");
    CHECK(isMeta(parseDeepLink("nuvio://watch/series/tt0944947"), "series",
                 "tt0944947"),
          "watch path");
    CHECK(isMeta(parseDeepLink("nuvio://meta/movie/tt0111161"), "movie",
                 "tt0111161"),
          "meta path fallback");
    CHECK(isMeta(parseDeepLink("nuvio://movie/tt0111161"), "movie",
                 "tt0111161"),
          "movie host");
    // ktor parameter names are case-sensitive (queryItemValue too).
    CHECK(parseDeepLink("NUVIO://META?TYPE=series&ID=tt1").kind ==
              DeepLinkKind::None,
          "uppercase query keys miss");
    CHECK(isMeta(parseDeepLink("nuvio://meta?type=film&id=imdb:tt0111161"),
                 "movie", "tt0111161"),
          "film alias + imdb prefix strip");
    CHECK(isMeta(parseDeepLink("nuvio://meta?tmdbId=1399&type=series"),
                 "series", "tmdb:1399"),
          "tmdb query id");
    CHECK(isMeta(parseDeepLink("nuvio://tmdb/1399?type=series"), "series",
                 "tmdb:1399"),
          "tmdb bare id + query type");
    CHECK(parseDeepLink("nuvio://meta?type=movie").kind ==
              DeepLinkKind::None,
          "missing id is none");
    CHECK(parseDeepLink("nuvio://meta?id=tt1").kind == DeepLinkKind::None,
          "missing type is none");
    CHECK(parseDeepLink("nuvio://unknownhost").kind == DeepLinkKind::None,
          "unknown host none");
    CHECK(parseDeepLink("nuvio:///meta?type=movie&id=tt1").kind ==
              DeepLinkKind::None,
          "empty host none");

    std::printf(failures ? "DEEPLINK SUITE FAILURES=%d\n"
                         : "DEEPLINK SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
