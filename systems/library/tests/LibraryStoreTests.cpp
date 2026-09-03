// Library + collections contract. ISOLATION: XDG sandbox (both stores
// write profile files).
#include <nuvio/library/CollectionStore.h>
#include <nuvio/library/LibraryStore.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>

using nuvio::library::CollectionStore;
using nuvio::library::LibraryStore;

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

    using namespace nuvio::library;

    { // item key + add/remove/isInLibrary + recency refresh
        CHECK(libraryItemKey("tt123", "Movie") == "movie:tt123",
              "key lowercases type");
        LibraryStore lib(1);
        CHECK(lib.count() == 0, "fresh profile empty");
        lib.addToLibrary("movie", "tt123", "Dune", "p", "d", 1000);
        lib.addToLibrary("series", "tt456", "Sev", "p2", "", 2000);
        CHECK(lib.count() == 2, "two rows added");
        CHECK(lib.isInLibrary("MOVIE", "tt123"), "lookup case-tolerant");
        CHECK(!lib.isInLibrary("movie", "tt999"), "missing reports false");
        lib.addToLibrary("movie", "tt123", "Dune", "p", "d", 3000);
        CHECK(lib.count() == 2, "re-add refreshes instead of duplicating");
        CHECK(lib.items()[0].savedAtEpochMs == 3000 ||
                  lib.items()[1].savedAtEpochMs == 3000,
              "recency refreshed");
        lib.removeFromLibrary("movie", "tt123");
        CHECK(!lib.isInLibrary("movie", "tt123") && lib.count() == 1,
              "remove drops the row");
        lib.removeFromLibrary("movie", "tt123");   // idempotent no-op
        CHECK(lib.count() == 1, "double remove stays quiet");

        // Dirty flags for the sync leg.
        CHECK(lib.pendingUpserts().size() == 1, "series row dirty");
        CHECK(lib.pendingDeletes().size() == 1, "movie row tombstoned");
        lib.clearPendingUpserts(lib.pendingUpserts());
        CHECK(lib.pendingUpserts().isEmpty(), "upserts cleared after push");

        // Cross-instance persistence (fresh view reads the file).
        LibraryStore view(1);
        CHECK(view.count() == 1 && view.isInLibrary("series", "tt456"),
              "library persists across instances");
        CHECK(view.pendingDeletes().size() == 1,
              "tombstones persist across instances");
    }

    { // codec: unknown members survive, garbage decodes empty
        const auto items = LibraryCodec::decodeItems(
            QStringLiteral("{\"items\":[{\"id\":\"tt1\",\"type\":\"movie\","
                           "\"name\":\"A\",\"posterShape\":\"Landscape\","
                           "\"traktRank\":7}],\"deltaCursorEventId\":9,"
                           "\"deltaInitialized\":true}"));
        CHECK(items.size() == 1 && items[0].id == "tt1",
              "item decodes");
        const QString enc = LibraryCodec::encodeItems(items);
        CHECK(enc.contains("\"posterShape\":\"Landscape\"") &&
                  enc.contains("\"traktRank\":7"),
              "unknown members preserved verbatim");
        CHECK(LibraryCodec::decodeItems("garbage").isEmpty(),
              "garbage decodes empty");
    }

    { // collections: CRUD + source edits + verbatim preservation
        CollectionStore cols(1);
        const QString cid = cols.createCollection("  Sci-Fi  ");
        CHECK(!cid.isEmpty(), "collection created with id");
        CHECK(cols.createCollection("   ").isEmpty(),
              "blank title rejected");
        cols.renameCollection(cid, "Science Fiction");
        const QString fid = cols.createFolder(cid, "Space Operas");
        CHECK(!fid.isEmpty(), "folder created");
        cols.addAddonSource(cid, fid, "cinemeta", "movie", "top", "");
        cols.addAddonSource(cid, fid, "cinemeta", "movie", "top", "");
        auto got = cols.collections();
        CHECK(got.size() == 1 && got[0].title == "Science Fiction" &&
                  got[0].folders.size() == 1 &&
                  got[0].folders[0].addonSources.size() == 1,
              "dedupe keeps one source");
        CHECK(got[0].folders[0].addonSources[0].catalogId == "top",
              "source fields stored");

        // TMDB-flavored folder survives edits byte-faithfully.
        cols.applyFromRemote(
            QStringLiteral("[{\"id\":\"c9\",\"title\":\"Mixed\","
                           "\"pinToTop\":true,\"folders\":[{\"id\":\"f9\","
                           "\"title\":\"F\",\"sources\":["
                           "{\"provider\":\"tmdb\",\"tmdbSourceType\":"
                           "\"discover\",\"title\":\"T\"},"
                           "{\"provider\":\"addon\",\"addonId\":\"a\","
                           "\"type\":\"movie\",\"catalogId\":\"top\"}]}]}]"));
        auto mixed = cols.collections();
        CHECK(mixed.size() == 1 && mixed[0].pinToTop &&
                  mixed[0].folders[0].addonSources.size() == 1 &&
                  mixed[0].folders[0].otherSources.size() == 1,
              "tmdb source preserved alongside addon source");
        cols.renameFolder("c9", "f9", "F2");
        cols.removeAddonSource("c9", "f9", 0);
        const QString out = cols.exportToJson();
        CHECK(out.contains("\"provider\":\"tmdb\"") &&
                  out.contains("\"tmdbSourceType\":\"discover\"") &&
                  !out.contains("\"catalogId\":\"top\""),
              "edits keep foreign sources, drop removed addon source");

        cols.moveCollection("c9", -1);   // single row: clamped no-op
        cols.setCollectionPinned("c9", false);
        CHECK(!cols.collections()[0].pinToTop, "unpin works");
        cols.removeFolder("c9", "f9");
        CHECK(cols.collections()[0].folders.isEmpty(), "folder removed");
        cols.removeCollection("c9");
        CHECK(cols.collections().isEmpty(), "collection removed");

        CollectionStore view(1);
        CHECK(view.collections().isEmpty(), "empty persists");
    }

    std::printf(failures ? "LIBRARY SUITE FAILURES=%d\n"
                         : "LIBRARY SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
