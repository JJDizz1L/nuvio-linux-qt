// Cloud mapping contract: playable tables, name fallbacks, grouping.
#include <nuvio/debrid/CloudLibrary.h>

#include <QCoreApplication>

#include <cstdio>

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
    using namespace nuvio::debrid;

    CHECK(cloudFilePlayable("a.mkv", ""), "mkv playable");
    CHECK(cloudFilePlayable("a.mp4", "video/mp4"), "mime wins");
    CHECK(cloudFilePlayable("a.MKV", ""), "case-insensitive extension");
    CHECK(cloudFilePlayable("a.ts", ""), "ts in the cloud table");
    CHECK(!cloudFilePlayable("a.srt", ""), "subtitles refused");
    CHECK(!cloudFilePlayable("a", ""), "extensionless refused");

    { // Torbox list: id fallback chain, name fallback, status priority
        const QByteArray body = R"({"success":true,"data":[
            {"id":7,"hash":"abc","name":"Show Pack",
             "status":"Cached","size":100,
             "files":[
               {"id":11,"short_name":"Show.S01E01.mkv","size":60},
               {"id":12,"name":"/dl/notes.txt","size":1}]},
            {"hash":"def","name":"",
             "files":[{"id":21,"absolute_path":"/dl/Movie.mp4"}]},
            {"name":"NoId"}]})";
        const auto items = parseTorboxCloudList(body, "Torrent");
        CHECK(items.size() == 2, "id-less row dropped");
        CHECK(items[0].id == "7" && items[0].name == "Show Pack" &&
                  items[0].status == "Cached" && items[0].sizeBytes == 100,
              "scalar id + direct size");
        CHECK(items[0].files.size() == 2, "both files mapped");
        CHECK(items[0].files[0].playable &&
                  items[0].files[0].name == "Show.S01E01.mkv",
              "short name preferred, playable");
        CHECK(!items[0].files[1].playable, "txt refused");
        CHECK(items[1].id == "def" && items[1].name == "def",
              "hash fallback id, id fallback name");
        CHECK(items[1].files[0].name == "Movie.mp4",
              "absolute path basenamed");
    }

    { // Torbox envelope failure -> empty
        CHECK(parseTorboxCloudList(
                  QByteArray("{\"success\":false}"), "Torrent").isEmpty() &&
                  parseTorboxCloudList(QByteArray("garbage"), "Torrent")
                      .isEmpty(),
              "failures yield nothing");
    }

    { // Premiumize listall: folder grouping, root files, sorting
        const QByteArray body = R"({"status":"success","files":[
            {"id":"f1","name":"A.mkv","path":"/Pack/A.mkv","size":10,
             "mime_type":"video/x-matroska"},
            {"id":"f2","name":"B.mp4","path":"/Pack/B.mp4","size":20},
            {"id":"f3","name":"Solo.avi","path":"Solo.avi","size":5},
            {"id":"f4","name":"","path":"/Pack/","size":1}]})";
        const auto items = parsePremiumizeCloudList(body);
        CHECK(items.size() == 2, "folder + root file group");
        const auto& folder = items[0].id.startsWith("folder:")
                                 ? items[0]
                                 : items[1];
        CHECK(folder.name == "Pack" && folder.files.size() == 2,
              "folder groups children");
        CHECK(folder.files[0].playable, "playable sorts first");
        const auto& root =
            items[0].id.startsWith("file:") ? items[0] : items[1];
        CHECK(root.name == "Solo.avi", "root file keeps its name");
        CHECK(parsePremiumizeCloudList(
                  QByteArray("{\"status\":\"error\"}")).isEmpty(),
              "error status yields nothing");
    }

    std::printf(failures ? "CLOUD SUITE FAILURES=%d\n"
                         : "CLOUD SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
