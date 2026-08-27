// Offline contract for the TorrServer wire/policy layer. Fixtures are the
// real JSON shapes + the Compose line's magnet test vectors - parity is the
// point: identical input MUST produce identical wire bytes.
#include <nuvio/p2p/TorrServerProtocol.h>

#include <QCoreApplication>
#include <cstdio>

using namespace nuvio::p2p;
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

    { // magnet URI canonicalization + encoding + dedup (Compose vectors)
        const QString hash =
            QStringLiteral("ABCDEF0123456789ABCDEF0123456789ABCDEF01");
        const QString tracker = QStringLiteral(
            "udp://tracker.example:80/announce?key=hello world");
        const QString m = buildMagnetUri(
            hash, {QString(), tracker, QStringLiteral(" "), tracker});
        CHECK(m.startsWith(QStringLiteral(
                  "magnet:?xt=urn:btih:abcdef0123456789abcdef0123456789"
                  "abcdef01")),
              "v1 hash canonicalized lowercase");
        CHECK(m.count(QStringLiteral("&tr=")) == 6,
              "5 defaults + deduped extra (blank/dup filtered)");
        CHECK(m.endsWith(QStringLiteral(
                  "udp%3A%2F%2Ftracker.example%3A80%2Fannounce%3Fkey%"
                  "3Dhello%20world")),
              "strict uppercase percent-encoding");
    }
    { // v2 multihash topic
        const QString h64(64, QLatin1Char('a'));
        CHECK(buildMagnetUri(h64)
                  .contains(QStringLiteral("xt=urn:btmh:1220") + h64),
              "64-hex uses btmh multihash topic");
    }
    { // invalid hashes rejected with empty result (no exceptions in C++)
        CHECK(buildMagnetUri(QStringLiteral("not-a-hash")).isEmpty(),
              "non-hex rejected");
        CHECK(buildMagnetUri(QString(39, QLatin1Char('a'))).isEmpty(),
              "wrong length rejected");
    }
    { // default trackers ride along in order
        const QString m = buildMagnetUri(QString(40, QLatin1Char('0')));
        CHECK(m.count(QStringLiteral("&tr=")) == 5,
              "five default trackers appended");
        CHECK(m.contains(QStringLiteral("udp%3A%2F%2Ftracker.opentrackr.org")),
              "opentrackr present");
    }
    { // request bodies
        const QByteArray add =
            addTorrentRequestBody(QStringLiteral("magnet:?x"));
        CHECK(add.contains("\"action\":\"add\""), "add body action");
        CHECK(add.contains("\"save_to_db\":false"),
              "add body never persists to db");
        CHECK(dropTorrentRequestBody(QStringLiteral("h1"))
                  .contains("\"action\":\"drop\""),
              "drop body action");
    }
    { // stats parsing over the real wire shape
        const QByteArray body =
            "{\"hash\":\"abc\",\"download_speed\":1048576,"
            "\"upload_speed\":2048,\"active_peers\":7,"
            "\"connected_seeders\":3,\"preloaded_bytes\":5242880,"
            "\"loaded_size\":10485760,\"torrent_size\":20971520,"
            "\"file_stats\":["
            "{\"id\":1,\"path\":\"Movie/sample.mkv\",\"length\":15000000},"
            "{\"id\":2,\"path\":\"Movie/trailer.mp4\",\"length\":5971520}]}";
        const auto s = parseTorrentStats(body);
        CHECK(s.has_value(), "stats body parses");
        if (s) {
            CHECK(s->peers == 7 && s->seeds == 3, "peer fields");
            CHECK(s->preloadedBytes == 5242880, "preload field");
            CHECK(s->files.size() == 2, "file list parsed");
            CHECK(s->files[1].id == 2
                      && s->files[1].path == QLatin1String("Movie/trailer.mp4"),
                  "file entries carry ids + paths");
        }
        CHECK(!parseTorrentStats(QByteArrayLiteral("garbage")).has_value(),
              "malformed body -> nullopt (not fatal)");
    }
    { // file-index precedence chain, in order
        using F = TorrentFile;
        const QList<F> files{
            {1, "Pack/readme.txt", 500},
            {2, "Pack/movie sample.mkv", 700000000},
            {3, "Pack/The Film 2024.mkv", 8000000000},
            {4, "Pack/extra.mp4", 100000000},
        };
        CHECK(resolveFileIndex(files, -1, QStringLiteral("the film 2024.mkv"))
                      == 3,
              "rule 1: exact basename case-insensitive");
        CHECK(resolveFileIndex(files, -1, QStringLiteral("2024")) == 3,
              "rule 2: contains match");
        CHECK(resolveFileIndex(files, 1, QString()) == 2,
              "rule 3: stremio idx+1 offset names an existing id");
        const QList<F> offsetGap{F{1, "Pack/readme.txt", 500},
                                 F{2, "Pack/movie sample.mkv", 700000000},
                                 F{4, "Pack/extra.mp4", 100000000}};
        CHECK(resolveFileIndex(offsetGap, 2, QString()) == 4,
              "rule 4: positional fallback when offset id is missing");
    }
    { // rules 5-7 on extension-driven data
        using F = TorrentFile;
        const QList<F> files{{1, "a.txt", 999999},
                             {2, "small.mp4", 10},
                             {3, "big.mkv", 777777}};
        CHECK(resolveFileIndex(files, -1, QString()) == 3,
              "rule 5: largest video wins over larger non-video");
        const QList<F> noVideo{{1, "doc.pdf", 50}, {2, "img.jpg", 40}};
        CHECK(resolveFileIndex(noVideo, -1, QString()) == 1,
              "rule 6: largest any when no video present");
        CHECK(resolveFileIndex({}, -1, QString()) == 1,
              "rule 7: empty file list still yields 1");
    }
    { // settings merge preserves unknown schema drift
        const QByteArray current =
            "{\"buffers_enable\":true,\"cache\":\"auto\",\"dl_limit\":0}";
        QByteArray updated;
        CHECK(mergeCacheSettings(current, 512, &updated),
              "valid settings merge");
        CHECK(QString::fromUtf8(updated).contains(QLatin1String("\"cache\":512")),
              "cache overridden as number");
        CHECK(QString::fromUtf8(updated).contains(QLatin1String("dl_limit")),
              "unknown fields preserved verbatim");
        CHECK(!mergeCacheSettings(QByteArrayLiteral("nope"), 1, nullptr),
              "non-object rejected without writing");
    }


    { // cache-size enum mapping (exact Compose numbers)
        CHECK(toTorrServerCacheMb(QStringLiteral("NONE")) == 64,
              "NONE floor");
        CHECK(toTorrServerCacheMb(QStringLiteral("GB_2")) == 2048,
              "GB_2 default");
        CHECK(toTorrServerCacheMb(QStringLiteral("GB_5")) == 5120, "GB_5");
        CHECK(toTorrServerCacheMb(QStringLiteral("GB_10")) == 10240,
              "GB_10");
        CHECK(toTorrServerCacheMb(QStringLiteral("garbage")) == 2048,
              "unknown falls back to default, never smaller");
    }

    std::printf(failures ? "P2P SUITE FAILURES=%d\n"
                         : "P2P SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
