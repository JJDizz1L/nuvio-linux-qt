// File selection contract: predicates, patterns, rule chain.
#include <nuvio/debrid/DebridFileSelect.h>

#include <QCoreApplication>

#include <cstdio>

using nuvio::debrid::TorrentFile;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {
TorrentFile file(int id, const QString& name, qint64 size = 0,
                 const QString& mime = {})
{
    TorrentFile f;
    f.id = id;
    f.name = name;
    f.size = size;
    f.mimeType = mime;
    return f;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace nuvio::debrid;

    CHECK(isPlayableVideo("Show.S01E02.mkv", ""), "mkv playable");
    CHECK(isPlayableVideo("anything.bin", "video/mp4"), "mime wins");
    CHECK(!isPlayableVideo("readme.nfo", ""), "nfo refused");
    // Compose Torbox rule: video/* mime short-circuits true, otherwise the
    // extension alone decides (even a lying audio/* mime + .mkv passes).
    CHECK(isPlayableVideo("Show.S01E02.mkv", "audio/mpeg"),
          "extension decides when mime is not video/*");
    CHECK((episodePatterns(1, 2) ==
           QStringList{"s01e02", "1x02", "1x2"}),
          "pattern triple");
    CHECK(episodePatterns(-1, 2).isEmpty(), "no season, no patterns");

    const QList<TorrentFile> files{
        file(0, "Show.S01E01.1080p.mkv", 100),
        file(1, "Show.S01E02.1080p.mkv", 200),
        file(2, "Show.S01E02.2160p.mkv", 900),
        file(3, "Sample.mkv", 10),
        file(4, "notes.txt", 1),
    };
    { // episode pattern narrows to E02 candidates, first wins
        const auto sel =
            selectTorrentFile(files, {}, 1, 2);
        CHECK(sel && sel->id == 1, "first E02 match wins");
    }
    { // specific names beat patterns (normalized contains either way;
      // note the wanted stem also matches E01/E02-1080p rows first, so a
      // discriminating token picks the intended file, Compose parity)
        const auto sel = selectTorrentFile(files, {"2160p"}, 1, 2);
        CHECK(sel && sel->id == 2, "specific token wins");
    }
    { // movies (no S/E): largest playable video
        const auto sel = selectTorrentFile(
            {file(0, "a.mkv", 100), file(1, "b.mkv", 900)}, {}, -1, -1);
        CHECK(sel && sel->id == 1, "largest wins without coordinates");
    }
    { // fileIdx override with off-by-one fallback
        const auto sel =
            selectTorrentFile(files, {}, -1, -1, 9);
        CHECK(!sel || sel->id != 9, "out-of-range idx falls through");
        const auto sel2 =
            selectTorrentFile(files, {}, -1, -1, 0);
        CHECK(sel2 && sel2->id == 0, "exact idx honored");
    }
    { // nothing playable -> empty
        CHECK(!selectTorrentFile({file(0, "x.txt", 5)}, {}, -1, -1),
              "unplayable set yields nothing");
    }

    std::printf(failures ? "FILESELECT SUITE FAILURES=%d\n"
                         : "FILESELECT SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
