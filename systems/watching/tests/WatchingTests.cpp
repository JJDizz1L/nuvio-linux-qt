// Offline contract for the watch-state foundation (explore/qtqml #2).
// Parity IS the point: identical storage shapes/keys as the Compose line.
#include <nuvio/watching/WatchProgress.h>
#include <nuvio/watching/WatchCodec.h>
#include <nuvio/watching/WatchingStore.h>
#include <nuvio/watching/WatchRecorder.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>

using namespace nuvio::watching;
static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

static long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Temp XDG so we never touch live Compose profile data.
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) { std::fprintf(stderr, "FAIL sandbox\n"); return 2; }
    const QByteArray cfgHome = QDir(sandbox.path()).filePath("config").toUtf8();
    qputenv("XDG_CONFIG_HOME", cfgHome);
    QDir().mkpath(QString::fromUtf8(cfgHome));

    { // progress-key format (Compose buildWatchProgressKey)
        CHECK(buildProgressKey("tt123", 1, 2) == "tt123_s1e2",
              "episode progress key format");
        CHECK(buildProgressKey("tt123", std::nullopt, std::nullopt) == "tt123",
              "movie progress key format");
        CHECK(buildProgressKey("tt123", 1, std::nullopt) == "tt123",
              "partial-season falls back to content id");
    }
    { // watched-key format (Compose watchedKey)
        CHECK(buildWatchedKey("series", "tt123", 1, 2) == "series:tt123:1:2",
              "episode watched key format");
        CHECK(buildWatchedKey("movie", "tt999", std::nullopt, std::nullopt)
                  == "movie:tt999:-1:-1",
              "movie watched key format");
    }
    { // buildPlaybackVideoId
        CHECK(buildPlaybackVideoId("tt123", 1, 2, std::nullopt) == "tt123:1:2",
              "episode playback video id composite");
        CHECK(buildPlaybackVideoId("tt123", std::nullopt, std::nullopt, "vid9")
              == "vid9", "fallback video id used");
        CHECK(buildPlaybackVideoId("tt123", std::nullopt, std::nullopt)
              == "tt123", "parent id for movies");
    }
    { // progressFraction (explicit % wins)
        WatchEntry e;
        e.lastPositionMs = 250'000; e.durationMs = 1'000'000;
        CHECK(e.progressFraction() == 0.25f, "fraction from position/duration");
        e.progressPercent = 42.0f;
        CHECK(e.progressFraction() == 0.42f, "explicit percent wins");
        WatchEntry plain;
        CHECK(plain.progressFraction() == 0.0f, "zero duration -> 0");
    }
    { // isEffectivelyCompleted (90% + flag + 100% pos)
        WatchEntry e; e.durationMs = 1'000;
        e.lastPositionMs = 900;  CHECK(e.isEffectivelyCompleted(), "90% completes");
        e = WatchEntry{}; e.durationMs = 1'000; e.lastPositionMs = 899;
        CHECK(!e.isEffectivelyCompleted(), "89% not complete");
        e = WatchEntry{}; e.durationMs = 1'000; e.lastPositionMs = 1'000;
        CHECK(e.isEffectivelyCompleted(), "100% position completes");
        e = WatchEntry{}; e.isCompleted = true;
        CHECK(e.isEffectivelyCompleted(), "completed flag completes");
        e = WatchEntry{}; e.progressPercent = 90.0f;
        CHECK(e.isEffectivelyCompleted(), "90% percent completes");
    }
    { // continueWatchingSelection: resumable-only, newest-per-key, fresh-first
        std::vector<WatchEntry> entries{
            {WatchEntry{"movie", "ttA", "", "ttA", "Alpha", {}, {}, {},
                          std::nullopt, std::nullopt, {}, {}, 10, 100, 1000,
                          {}, {}, {}, {}, {}, {}, false, {}, "local", {}, {}, {}, {}}},
            {WatchEntry{"movie", "ttA", "", "ttA", "Alpha", {}, {}, {},
                          std::nullopt, std::nullopt, {}, {}, 20, 100, 2000,
                          {}, {}, {}, {}, {}, {}, false, {}, "local", {}, {}, {}, {}}},
            {WatchEntry{"movie", "ttB", "", "ttB", "Bravo", {}, {}, {},
                          std::nullopt, std::nullopt, {}, {}, 10, 100, 500,
                          {}, {}, {}, {}, {}, {}, false, {}, "local", {}, {}, {}, {}}},
            {WatchEntry{"movie", "ttC", "", "ttC", "Carol", {}, {}, {},
                          std::nullopt, std::nullopt, {}, {}, 100, 100, 3000,
                          {}, {}, {}, {}, {}, {}, true, {}, "local", {}, {}, {}, {}}},
        };
        auto cw = continueWatchingSelection(entries, 20);
        CHECK(cw.size() == 2, "completed excluded; dedupe -> 2");
        CHECK(cw.front().title == "Alpha", "newest Alpha first");
        CHECK(cw.front().lastPositionMs == 20, "newer Alpha wins tie");
        CHECK(cw.back().title == "Bravo", "older Bravo last");
                CHECK(continueWatchingSelection(entries, 1).size() == 1, "limit applied");
    }
    { // codec round-trip (Compose shapes, camelCase, encodeDefaults, null-omit)
        std::vector<WatchEntry> entries{
            {WatchEntry{"series", "tt123", "show", "tt123:1:2", "S1E2",
                          std::nullopt, std::optional<std::string>("p.jpg"),
                          std::nullopt, std::optional<int>(1), std::optional<int>(2),
                          std::optional<std::string>("Pilot"), std::nullopt,
                          480'000, 600'000, 1'700'000'000LL,
                          {}, {}, {}, {}, {}, {},
                          false, std::optional<float>(80.0f), "local", {}, {}, {}, "tt123_s1e2"}},
        };
        const QString enc = WatchCodec::encodeProgress(entries);
        CHECK(enc.contains("\"seasonNumber\":1") && enc.contains("\"episodeNumber\":2"),
              "codec writes season/episode");
        CHECK(enc.contains("\"progressKey\":\"tt123_s1e2\""), "progressKey emitted");
        CHECK(enc.contains("\"lastSuccessfulPushEpochMs\":0") &&
              enc.contains("\"deltaInitialized\":false"),
              "payload defaults present (encodeDefaults=true)");
        CHECK(!enc.contains("\"logo\""), "null optional omitted");
        const auto dec = WatchCodec::decodeProgress(enc);
        CHECK(dec.entries.size() == 1, "round-trip entry count");
        CHECK(dec.entries.front().progressPercent.has_value() &&
              *dec.entries.front().progressPercent == 80.0f,
              "round-trip percent preserved");
        CHECK(dec.entries.front().resolvedProgressKey() == "tt123_s1e2",
              "round-trip key resolves");
        CHECK(!dec.entries.front().logo.has_value(),
              "absent optional decodes to nullopt");
        CHECK(WatchCodec::decodeProgress(
                  "{\"entries\":[],\"bogusExtra\":1,\"deltaInitialized\":true}")
                  .deltaInitialized,
              "unknown top-level keys tolerated (ignoreUnknownKeys)");
        CHECK(WatchCodec::decodeProgress("not json").entries.empty(),
              "parse failure -> empty (not fatal)");
    }
    { // watched codec round-trip
        std::vector<WatchedItem> items{
            {"series", "tt123", "Show", std::optional<std::string>("po.jpg"),
             std::nullopt, std::optional<int>(1), std::optional<int>(2),
             std::optional<std::string>("vid"), 1'700'000'000LL},
        };
        const QString enc = WatchCodec::encodeWatched(items);
        CHECK(enc.contains("\"type\":\"series\"") && enc.contains("\"season\":1")
              && enc.contains("\"episode\":2"), "watched camelCase fields");
        const auto dec = WatchCodec::decodeWatched(enc);
        CHECK(dec.size() == 1, "watched round-trip count");
        CHECK(buildWatchedKey(dec.front().type, dec.front().id,
                              dec.front().season, dec.front().episode)
                  == "series:tt123:1:2",
                            "watched key derived from round-trip items");
    }
    { // WatchingStore: resume persist + watched flags (cross-instance readable)
        QTemporaryDir sd;
        CHECK(sd.isValid(), "temp store dir");
        const QString p = QDir(sd.path()).filePath("watch_progress.properties");
        const QString w = QDir(sd.path()).filePath("watched.properties");
        WatchingStore store(p, w, 1);
        CHECK(store.loadEntries().empty(), "fresh store empty");

        WatchEntry e;
        e.contentType = "series"; e.parentMetaId = "tt123"; e.parentMetaType = "show";
        e.videoId = "tt123:1:2"; e.title = "S1E2"; e.season = 1; e.episode = 2;
        e.poster = "p.jpg"; e.lastPositionMs = 300'000; e.durationMs = 1'000'000;
        e.lastUpdatedEpochMs = 1'700'000'000LL; e.source = "local";
        store.upsert(e);

        WatchingStore store2(p, w, 1);
        auto entries = store2.loadEntries();
        CHECK(entries.size() == 1, "cross-instance resume persisted");
        CHECK(entries.front().resolvedProgressKey() == "tt123_s1e2",
              "persisted key resolved");
        CHECK(entries.front().lastPositionMs == 300'000, "persisted position");

        store2.markWatched("series", "tt123", 1, 2, 1'700'000'001LL);
        CHECK(store2.isWatched("series", "tt123", 1, 2), "watched persists");
        store2.markWatched("series", "tt123", 1, 2, 1'700'000'002LL);
        auto keys = store2.watchedKeys();
        CHECK(keys.size() == 1 && keys.front() == "series:tt123:1:2",
              "idempotent mark keeps single watched key");
        store2.unmarkWatched("series", "tt123", 1, 2);
        CHECK(!store2.isWatched("series", "tt123", 1, 2), "unmark works");

        // completed entries are excluded from the continue-watching surface
        WatchEntry done = e; done.isCompleted = true; done.progressPercent = 100.0f;
        store2.upsert(done);
        const auto fresh = continueWatchingSelection(store2.loadEntries(), 20);
        CHECK(fresh.empty(), "completed entry excluded from continue-watching");
    }
    { // WatchRecorder: session -> publish -> complete -> watched + resume dropped
        QTemporaryDir sd;
        CHECK(sd.isValid(), "recorder temp dir");
        const QString p = QDir(sd.path()).filePath("watch_progress.properties");
        const QString w = QDir(sd.path()).filePath("watched.properties");
        WatchingStore store(p, w, 1);
        WatchRecorder rec(&store, &app);
        const long long t0 = nowMs();
        rec.beginSession("series", "tt9", "show", "tt9:1:1", "Ep1", 1, 1, "", t0);
        rec.publishPosition(500, 1'000'000);            // below 1s -> no-op
        CHECK(store.loadEntries().empty(), "sub-1s position not persisted");
        rec.publishPosition(2'000, 1'000'000);
        {
            const auto cw = rec.continueWatching();
            CHECK(cw.size() == 1, "resume surfaced after publish");
            bool keyOk = false;
            for (const auto& item : cw)
                keyOk = keyOk || item.toMap().value("progressKey") == "tt9_s1e1";
            CHECK(keyOk, "cw key for episode is tt9_s1e1");
        }
        // 2s -> 12s advances >=10s (debounce) -> persisted.
        rec.publishPosition(12'000, 1'000'000);
        CHECK(store.loadEntries().front().lastPositionMs == 12'000,
              "debounced (>=10s) update persisted");
        rec.publishPosition(950'000, 1'000'000);
        rec.endSessionCompleted(t0 + 1);
        CHECK(store.loadEntries().empty(), "completed session drops resume row");
        CHECK(rec.isWatched("series", "tt9", 1, 1),
              "completed session marks watched");
    }
    { // continueWatching rows carry artwork fields (wide-card artwork strip)
        QTemporaryDir sd;
        CHECK(sd.isValid(), "artwork temp dir");
        const QString p = QDir(sd.path()).filePath("watch_progress.properties");
        const QString w = QDir(sd.path()).filePath("watched.properties");
        WatchingStore store(p, w, 1);
        WatchRecorder rec(&store, &app);

        WatchEntry ep;
        ep.contentType = "series"; ep.parentMetaId = "tt5";
        ep.parentMetaType = "series"; ep.videoId = "tt5:1:2";
        ep.title = "Show"; ep.season = 1; ep.episode = 2;
        ep.episodeThumbnail = "https://img/ep.jpg";
        ep.background = "https://img/bg.jpg";
        ep.lastPositionMs = 60'000; ep.durationMs = 1'800'000;
        ep.lastUpdatedEpochMs = nowMs(); ep.source = "local";
        store.upsert(ep);

        WatchEntry mv = ep;
        mv.contentType = "movie"; mv.parentMetaId = "tt6";
        mv.parentMetaType = "movie"; mv.videoId = "tt6";
        mv.season = std::nullopt; mv.episode = std::nullopt;
        mv.episodeThumbnail = std::nullopt;
        mv.poster = "https://img/poster.jpg";
        store.upsert(mv);

        const auto cw = rec.continueWatching();
        CHECK(cw.size() == 2, "two resumable rows surfaced");
        for (const auto& item : cw) {
            const auto m = item.toMap();
            if (m.value("id") == QLatin1String("tt5")) {
                CHECK(m.value("artwork") == QLatin1String("https://img/ep.jpg"),
                      "episode artwork prefers episodeThumbnail");
                CHECK(m.value("background") == QLatin1String("https://img/bg.jpg"),
                      "background exposed for fallback tier");
            }
            if (m.value("id") == QLatin1String("tt6")) {
                CHECK(m.value("artwork") == QLatin1String("https://img/poster.jpg"),
                      "movie artwork prefers poster");
            }
        }
    }
    { // resumePositionMsFor: identity-scoped, resumable-only
        QTemporaryDir sd;
        CHECK(sd.isValid(), "resume lookup temp dir");
        const QString p = QDir(sd.path()).filePath("watch_progress.properties");
        const QString w = QDir(sd.path()).filePath("watched.properties");
        WatchingStore store(p, w, 1);
        WatchRecorder rec(&store, &app);

        WatchEntry e;
        e.contentType = "movie"; e.parentMetaId = "tt77"; e.parentMetaType = "movie";
        e.videoId = "tt77"; e.title = "Movie"; e.lastPositionMs = 123'456;
        e.durationMs = 1'000'000; e.lastUpdatedEpochMs = nowMs(); e.source = "local";
        store.upsert(e);

        CHECK(rec.resumePositionMsFor("tt77") == 123'456,
              "movie resume position found");
        CHECK(rec.resumePositionMsFor("tt78") == 0, "unknown id -> 0");
        CHECK(rec.resumePositionMsFor("tt77", -1, -1) == 123'456,
              "explicit -1/-1 matches movie");
        CHECK(rec.resumePositionMsFor("tt77", 1, 1) == 0,
              "episode query misses movie row");

        // completed rows are not resumable
        WatchEntry done = e; done.isCompleted = true; done.progressPercent = 100.0f;
        store.upsert(done);
        CHECK(rec.resumePositionMsFor("tt77") == 0,
              "completed row yields no resume");
    }

    std::printf(failures ? "WATCHING SUITE FAILURES=%d\n"
                         : "WATCHING SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}



