// StreamLinkCache contract: key shapes, Java-fold hash, credential table,
// freshness/eviction. ISOLATION: XDG_CONFIG_HOME redirected to temp.
#include <nuvio/playback/StreamLinkCache.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>

using nuvio::playback::CachedLink;
using nuvio::playback::StreamLinkCache;

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
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME",
            QDir(sandbox.path()).filePath("cfg").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

    using namespace nuvio::playback;

    { // content-key shapes (verbatim repository rules)
        CHECK(streamLinkContentKey("movie", "tt123") == "movie|tt123",
              "movie key");
        CHECK(streamLinkContentKey("SERIES", "tt123:1:2", "tt123", 1, 2) ==
                  "series|tt123|s1|e2|tt123:1:2",
              "episode key lowercases type");
        CHECK(streamLinkContentKey("series", "tt123", "", 1, 2) ==
                  "series|tt123",
              "missing parent falls back to bare");
        // Java-fold stability pin (hand-computed fold of "movie|tt123").
        unsigned long long acc = 0;
        for (const QChar c : QStringLiteral("movie|tt123"))
            acc = acc * 31ULL + c.unicode();
        CHECK(streamLinkHashedKey("movie|tt123") ==
                  "stream_link_" + QString::number(acc),
              "hash matches the fold definition");
    }

    { // credential table (verbatim key set)
        CHECK(urlHasExpiringCredentials("https://x/y.m3u8?exp=99&foo=1"),
              "exp detected");
        CHECK(urlHasExpiringCredentials("https://x/y?Signature=abc"),
              "case-insensitive Signature");
        CHECK(urlHasExpiringCredentials("https://x/y?mytoken=1"),
              "token fragment");
        CHECK(urlHasExpiringCredentials("https://x/y?e=abc"),
              "single-letter e");
        CHECK(!urlHasExpiringCredentials("https://x/y.m3u8?foo=1&bar=2"),
              "plain query clean");
        CHECK(!urlHasExpiringCredentials("https://x/y.m3u8"),
              "no query clean");
        CHECK(!urlHasExpiringCredentials("https://x/tokeny.m3u8"),
              "path tokens ignored (query only)");
    }

    { // save/getValid round-trip + eviction rules
        StreamLinkCache cache(1);
        CachedLink link;
        link.url = "https://cdn.example/v.mp4";
        link.streamName = "1080p";
        link.addonName = "Demo";
        link.addonId = "demo";
        cache.save("movie|tt1", link, 1000000);

        const auto fresh =
            cache.getValid("movie|tt1", 24LL * 3600 * 1000, 1000000 + 1000);
        CHECK(fresh && fresh->url == link.url &&
                  fresh->addonName == "Demo" && fresh->cachedAtMs == 1000000,
              "fresh entry round-trips");
        CHECK(!cache.getValid("movie|tt1", 0, 2000000),
              "zero max-age disables");
        CHECK(!cache.getValid("movie|tt1", 1000, 1000000 + 1001),
              "stale entry evicted");
        // Second read after eviction still misses (eviction persisted).
        CHECK(!cache.getValid("movie|tt1", 24LL * 3600 * 1000, 1000001),
              "eviction sticks");

        // Expiring urls are never stored.
        CachedLink exp = link;
        exp.url = "https://cdn.example/v.mp4?token=abc";
        cache.save("movie|tt2", exp, 1000000);
        CHECK(!cache.getValid("movie|tt2", 24LL * 3600 * 1000, 1000001),
              "expiring url refused");

        // Profile isolation: profile 2 never sees profile 1 rows.
        StreamLinkCache cache2(2);
        cache.save("movie|tt3", link, 1000000);
        CHECK(!cache2.getValid("movie|tt3", 24LL * 3600 * 1000, 1000001),
              "profile-scoped keys isolate");

        // Cross-instance persistence (fresh view reads the file).
        StreamLinkCache view(1);
        CHECK(view.getValid("movie|tt3", 24LL * 3600 * 1000, 1000001)
                  ->url == link.url,
              "cache persists across instances");
    }

    std::printf(failures ? "LINKCACHE SUITE FAILURES=%d\n"
                         : "LINKCACHE SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
