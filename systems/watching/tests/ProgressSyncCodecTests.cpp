// Offline contract: watch-progress wire codec vs SupabaseProgressSyncAdapter.
#include <nuvio/watching/ProgressSyncCodec.h>

#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>

using nuvio::watching::ProgressSyncCodec;
using nuvio::watching::WatchEntry;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {
WatchEntry movieEntry()
{
    WatchEntry e;
    e.parentMetaId = "tt0993846";
    e.contentType  = "movie";
    e.videoId      = "tt0993846";
    e.lastPositionMs = 740'000;
    e.durationMs     = 7'380'000;
    e.lastUpdatedEpochMs = 1'756'000'000'000;
    return e;
}
} // namespace

int main()
{
    { // push entry: snake_case, explicit nulls for absent season/episode
        const auto o = ProgressSyncCodec::syncEntryJson(movieEntry());
        CHECK(o.value("content_id").toString() == "tt0993846", "content_id");
        CHECK(o.value("season").isNull(), "absent season serializes null");
        CHECK(o.value("episode").isNull(), "absent episode serializes null");
        CHECK(static_cast<long long>(o.value("position").toDouble())
                  == 740000, "position as number");
        CHECK(o.value("progress_key").toString() == "tt0993846",
              "resolved progress_key");

        // Episode entry: both ints present.
        auto ep = movieEntry();
        ep.parentMetaId = "tt1112694";
        ep.contentType  = "series";
        ep.season = 2;
        ep.episode = 5;
        const auto oe = ProgressSyncCodec::syncEntryJson(ep);
        CHECK(oe.value("season").toInt() == 2, "episode season int");
        CHECK(oe.value("progress_key").toString() == "tt1112694_s2e5",
              "composite progress key");
    }

    { // param builders
        const auto push = ProgressSyncCodec::pushParams(
            1, {movieEntry()}, QStringLiteral("nuvio-mobile-test"));
        CHECK(push.value("p_profile_id").toInt() == 1, "push profile param");
        CHECK(push.value("p_origin_client_id").toString()
                  == "nuvio-mobile-test", "push origin param");
        CHECK(push.value("p_entries").toArray().size() == 1,
              "push entries array");

        const auto del = ProgressSyncCodec::deleteParams(
            1, {"tt0993846", "tt1112694_s2e5"},
            QStringLiteral("nuvio-mobile-test"));
        CHECK(del.value("p_keys").toArray().size() == 2, "delete keys array");

        const auto cur = ProgressSyncCodec::cursorParams(1);
        CHECK(cur.size() == 1 && cur.value("p_profile_id").toInt() == 1,
              "cursor params minimal");

        const auto dp = ProgressSyncCodec::deltaPullParams(1, 420, 200);
        CHECK(static_cast<long long>(
                  dp.value("p_since_event_id").toDouble()) == 420,
              "delta since param");
        CHECK(dp.value("p_limit").toInt() == 200, "delta limit param");

        const auto fp = ProgressSyncCodec::fullPullParams(1);
        CHECK(fp.size() == 1, "full pull omits null optionals");
    }

    { // decode: bare-number cursor + delta rows + full-pull records
        CHECK(ProgressSyncCodec::parseCursor("4200").value_or(-1) == 4200,
              "bare-number cursor parses");
        CHECK(ProgressSyncCodec::parseCursor(" 4200 \n").value_or(-1) == 4200,
              "whitespace tolerated");
        CHECK(!ProgressSyncCodec::parseCursor("{}").has_value(),
              "object cursor rejected");
        CHECK(!ProgressSyncCodec::parseCursor("").has_value(),
              "empty cursor rejected");

        QJsonArray rows;
        rows.append(QJsonObject{
            {QStringLiteral("event_id"), 9},
            {QStringLiteral("operation"), QStringLiteral("upsert")},
            {QStringLiteral("progress_key"), QStringLiteral("tt1_s1e2")},
            {QStringLiteral("content_id"), QStringLiteral("tt1")},
            {QStringLiteral("content_type"), QStringLiteral("series")},
            {QStringLiteral("video_id"), QStringLiteral("tt1")},
            {QStringLiteral("season"), 1},
            {QStringLiteral("episode"), 2},
            {QStringLiteral("position"), 500.0},
            {QStringLiteral("duration"), 1000.0},
            {QStringLiteral("last_watched"), 777.0}});
        rows.append(QJsonObject{
            {QStringLiteral("event_id"), 10},
            {QStringLiteral("operation"), QStringLiteral("delete")},
            {QStringLiteral("progress_key"), QStringLiteral("tt2")}});
        const auto evs = ProgressSyncCodec::decodeDeltas(QJsonDocument(rows));
        CHECK(evs.size() == 2, "two delta events");
        CHECK(evs[0].eventId == 9 && evs[0].season.value_or(-1) == 1,
              "delta fields mapped");
        CHECK(evs[1].operation == "delete" && evs[1].progressKey == "tt2",
              "delete event mapped");

        QJsonArray recArr;
        recArr.append(QJsonObject{
            {QStringLiteral("content_id"), QStringLiteral("tt3")},
            {QStringLiteral("content_type"), QStringLiteral("movie")},
            {QStringLiteral("video_id"), QStringLiteral("tt3")},
            {QStringLiteral("season"), QJsonValue::Null},
            {QStringLiteral("episode"), QJsonValue::Null},
            {QStringLiteral("position"), 42.0},
            {QStringLiteral("duration"), 100.0},
            {QStringLiteral("last_watched"), 555.0},
            {QStringLiteral("progress_key"), QStringLiteral("tt3")}});
        const auto recs =
            ProgressSyncCodec::decodeRecords(QJsonDocument(recArr));
        CHECK(recs.size() == 1 && recs[0].parentMetaId == "tt3",
              "full-pull record mapped");
        CHECK(recs[0].lastUpdatedEpochMs == 555, "last_watched mapped");
    }

    std::printf(failures ? "PROGRESS-CODEC SUITE FAILURES=%d\n"
                         : "PROGRESS-CODEC SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}