// Offline contract for episode-release notifications: date kernel,
// JVM-hash ids, body shapes, request gating, codec, and library
// reconcile (XDG-sandboxed, no network - refresh paths untested here).
#include <nuvio/library/LibraryStore.h>
#include <nuvio/notifications/ReleaseDate.h>
#include <nuvio/notifications/ReleaseNotifications.h>
#include <nuvio/settings/PropertiesStore.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

using nuvio::notifications::buildMetaDeepLinkUrl;
using nuvio::notifications::buildNotificationBody;
using nuvio::notifications::buildNotificationId;
using nuvio::notifications::buildRequestsForShow;
using nuvio::notifications::buildTrackedShowKey;
using nuvio::notifications::decodePayload;
using nuvio::notifications::encodePayload;
using nuvio::notifications::isSeriesLibraryType;
using nuvio::notifications::jvmStringHash;
using nuvio::notifications::normalizeSeriesType;
using nuvio::notifications::parseEpisodeReleaseLocalDate;
using nuvio::notifications::parseIsoCalendarDate;
using nuvio::notifications::ReleaseNotificationManager;
using nuvio::notifications::ReleaseRequest;
using nuvio::notifications::TrackedShow;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME",
            QDir(sandbox.path()).filePath("cfg").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

    { // date kernel (fork parser rules verbatim)
        CHECK(parseIsoCalendarDate("2024-03-01") == "2024-03-01",
              "plain date passes through");
        CHECK(parseIsoCalendarDate(" 2024-03-01 ").isEmpty() == false,
              "plain date trims");
        CHECK(parseIsoCalendarDate("2024-02-30").isEmpty(),
              "impossible day rejected");
        CHECK(parseIsoCalendarDate("2024-13-01").isEmpty(),
              "month 13 rejected");
        CHECK(parseIsoCalendarDate("2024-3-1").isEmpty(),
              "non-padded rejected");
        CHECK(parseIsoCalendarDate("").isEmpty(), "empty rejected");
        CHECK(parseEpisodeReleaseLocalDate("2024-03-01") == "2024-03-01",
              "release plain date");
        CHECK(parseEpisodeReleaseLocalDate("") .isEmpty(),
              "release empty");
        // Noon UTC renders the same calendar date in every inhabited zone.
        CHECK(parseEpisodeReleaseLocalDate("2024-03-01T12:00:00Z") ==
                  "2024-03-01",
              "zoned noon UTC");
        CHECK(parseEpisodeReleaseLocalDate("2024-03-01T12:00:00+0200") ==
                  "2024-03-01",
              "bare offset normalized");
        CHECK(parseEpisodeReleaseLocalDate("2024-03-01T12:00:00") ==
                  "2024-03-01",
              "zone-less contributes date part");
        CHECK(parseEpisodeReleaseLocalDate("aired 2024-03-01 (us)") ==
                  "2024-03-01",
              "embedded date fallback");
        CHECK(parseEpisodeReleaseLocalDate("soon").isEmpty(),
              "garbage rejected");
    }

    { // JVM hash vectors (python-verified java.lang.String.hashCode)
        CHECK(jvmStringHash("") == 0, "empty hash");
        CHECK(jvmStringHash("series:tt123") == -190694161,
              "content hash exact");
        CHECK(jvmStringHash("tt500:2:3") == -1990390682,
              "episode hash exact");
        CHECK(jvmStringHash("series:tt0944947") == 92173776,
              "positive hash exact");
    }

    { // keys / types / ids / bodies / links
        CHECK(normalizeSeriesType("TV") == "series", "tv folds");
        CHECK(normalizeSeriesType("Show") == "series", "show folds");
        CHECK(normalizeSeriesType("tvshow") == "series", "tvshow folds");
        CHECK(normalizeSeriesType("movie") == "movie", "movie kept");
        CHECK(buildTrackedShowKey("TV", " tt123 ") == "series:tt123",
              "key normalizes + trims");
        CHECK(isSeriesLibraryType("series") && !isSeriesLibraryType("movie"),
              "series gate");
        CHECK(buildNotificationId(1, "series", "tt123", "tt500:2:3",
                                  "2024-03-01") ==
                  "episode-release-1-190694161-1990390682-2024-03-01",
              "notification id shape");
        CHECK(buildNotificationId(1, "series", "tt123", "",
                                  "2024-03-01") ==
                  "episode-release-1-190694161-613282050-2024-03-01",
              "blank episode id falls back to date");
        CHECK(buildNotificationBody(1, 2, "Pilot") ==
                  "S1E2 • Pilot is out now",
              "full body");
        CHECK(buildNotificationBody(1, 2, "") == "S1E2 is out now",
              "code-only body");
        CHECK(buildNotificationBody(-1, 5, "") == "E5 is out now",
              "episode-only body");
        CHECK(buildNotificationBody(-1, -1, "Finale") ==
                  "Finale is out now",
              "title-only body");
        CHECK(buildNotificationBody(-1, -1, "") ==
                  "A new episode is out now",
              "generic body");
        CHECK(buildMetaDeepLinkUrl("series", "tt123") ==
                  "nuvio://meta?type=series&id=tt123",
              "deep link shape");
    }

    { // request gating over meta-shaped videos
        TrackedShow show;
        show.contentId = "tt1";
        show.contentType = "series";
        show.followedOnIsoDate = "2024-01-10";
        const QVariantList videos{
            QVariantMap{{"id", "tt1:1:1"},
                        {"season", 1},
                        {"episode", 1},
                        {"name", "Pilot"},
                        {"released", "2024-01-05"},
                        {"thumb", "http://x/t.jpg"}},
            QVariantMap{{"id", "tt1:1:2"},
                        {"season", 1},
                        {"episode", 2},
                        {"name", "Second"},
                        {"released", "2024-01-12"},
                        {"thumb", ""}},
            QVariantMap{{"id", "tt1:1:3"},
                        {"season", 1},
                        {"episode", 3},
                        {"name", "NoDate"},
                        {"released", ""},
                        {"thumb", ""}},
            QVariantMap{{"id", "tt1:0:0"},
                        {"name", "NoSeason"},
                        {"released", "2024-02-01"},
                        {"thumb", ""}},
        };
        const QList<ReleaseRequest> reqs =
            buildRequestsForShow(1, show, "Show", videos);
        CHECK(reqs.size() == 1, "only post-follow dated episodes survive");
        CHECK(reqs[0].notificationTitle == "Show", "show title carried");
        CHECK(reqs[0].notificationBody == "S1E2 • Second is out now",
              "body built");
        CHECK(reqs[0].releaseDateIso == "2024-01-12", "date carried");
        CHECK(reqs[0].deepLinkUrl ==
                  "nuvio://meta?type=series&id=tt1",
              "link carried");
        CHECK(reqs[0].backdropUrl == "", "empty thumb stays empty");
    }

    { // codec: round-trip, sorted persist, garbage tolerance
        QList<TrackedShow> shows{
            {QStringLiteral("ttB"), QStringLiteral("series"),
             QStringLiteral("2024-01-02")},
            {QStringLiteral("ttA"), QStringLiteral("movie"),
             QStringLiteral("2024-01-01")},
        };
        const QString enc = encodePayload(true, shows);
        const auto dec = decodePayload(enc);
        CHECK(dec.enabled, "enabled round-trips");
        CHECK(dec.shows.size() == 2 &&
                  dec.shows[0].contentId == "ttA" &&
                  dec.shows[1].contentId == "ttB",
              "persist sorts by (type,id)");
        CHECK(decodePayload("").shows.isEmpty(), "empty decodes empty");
        CHECK(decodePayload("{garbage").shows.isEmpty() &&
                  !decodePayload("{garbage").enabled,
              "garbage decodes empty");
    }

    { // reconcile: series tracked, movies ignored, removals dropped
        nuvio::library::LibraryStore library(1);
        ReleaseNotificationManager mgr(&library);
        mgr.setEnabled(false);   // loads without touching the network
        library.addToLibrary("series", "ttS", "Show", "", "", 0);
        library.addToLibrary("movie", "ttM", "Film", "", "", 0);
        QCoreApplication::processEvents();
        nuvio::settings::PropertiesStore store(
            nuvio::settings::PropertiesStore::defaultPath(
                "episode_release_notifications"));
        const auto raw = store.getString("episode_release_notifications_1");
        CHECK(raw.has_value(), "reconcile persists");
        const auto dec = decodePayload(
            raw ? QString::fromStdString(*raw) : QString());
        CHECK(dec.shows.size() == 1 && dec.shows[0].contentId == "ttS",
              "series tracked, movie ignored");
        library.removeFromLibrary("series", "ttS");
        QCoreApplication::processEvents();
        // Fresh store view: instances snapshot at construction.
        nuvio::settings::PropertiesStore store2(
            nuvio::settings::PropertiesStore::defaultPath(
                "episode_release_notifications"));
        const auto raw2 = store2.getString("episode_release_notifications_1");
        CHECK(decodePayload(raw2 ? QString::fromStdString(*raw2) : QString())
                  .shows.isEmpty(),
              "removal drops the tracked row");
    }

    std::printf(failures ? "NOTIFICATIONS SUITE FAILURES=%d\n"
                         : "NOTIFICATIONS SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
