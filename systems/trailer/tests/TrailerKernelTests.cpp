// Offline contract for the trailer extraction kernel: candidate
// classification, scoring order, visionos-first partitioning, and the
// four-tier source policy - all over fixture player-response bodies.
#include <nuvio/trailer/TrailerKernel.h>

#include <QCoreApplication>
#include <cstdio>

using namespace nuvio::trailer;
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

    {
        CHECK(extractVideoId(QStringLiteral("dQw4w9WgXcQ"))
                  == QLatin1String("dQw4w9WgXcQ"),
              "bare id accepted");
        CHECK(extractVideoId(QStringLiteral(
                  "https://youtu.be/dQw4w9WgXcQ?t=1"))
                  == QLatin1String("dQw4w9WgXcQ"),
              "youtu.be path extracted");
        CHECK(extractVideoId(QStringLiteral("https://www.youtube.com/"
                                            "watch?v=dQw4w9WgXcQ"))
                  == QLatin1String("dQw4w9WgXcQ"),
              "watch?v= extracted");
        CHECK(extractVideoId(QStringLiteral("too short")).isEmpty(),
              "garbage rejected");
    }
    {
        CHECK(clients().size() == 4 && clients().first().key
                  == QStringLiteral("visionos"),
              "client chain starts at visionos");
        const auto& vr = clients()[1];
        CHECK(QByteArray(vr.id) == QByteArray("28"),
              "android_vr client id");
        CHECK(!fallbackInnertubeApiKey().isEmpty(), "fallback key present");

        const auto body = playerRequestBody(clients().first(),
                                            QStringLiteral("abc12345678"));
        CHECK(body.contains("\"videoId\":\"abc12345678\""), "body video id");
        CHECK(body.contains("\"clientName\":\"VISIONOS\""),
              "body carries client context");
        const auto headers =
            playerRequestHeaders(clients().first());
        CHECK(headers.size() == 5, "five headers (no visitor data)");
    }
    {
        const QByteArray body =
            "{\"streamingData\":{"
            "\"hlsManifestUrl\":\"https://manifest.googlevideo.com/api/playlist.m3u8\","
            "\"formats\":["
            "{\"url\":\"https://g/p720.mp4\",\"mimeType\":\"video/mp4\","
            "\"qualityLabel\":\"720p\",\"bitrate\":2000000}],"
            "\"adaptiveFormats\":["
            "{\"url\":\"https://g/v1080.mp4\",\"mimeType\":\"video/mp4\","
            "\"height\":1080,\"fps\":30,\"bitrate\":4000000},"
            "{\"url\":\"https://g/vwebm.webm\",\"mimeType\":\"video/webm\","
            "\"height\":720,\"fps\":30,\"bitrate\":2500000},"
            "{\"url\":\"https://g/a.m4a\",\"mimeType\":\"audio/mp4\","
            "\"bitrate\":130000,\"audioSampleRate\":44100}]}}";
        const auto b = parseStreamingData(body);
        CHECK(b.progressive.size() == 1 && b.video.size() == 2
                  && b.audio.size() == 1,
              "buckets classified");
        CHECK(b.hlsManifestUrl.startsWith("https://manifest."),
              "hls manifest captured");
        CHECK(b.video[0].height == 1080 && b.video[0].ext == "mp4",
              "adaptive video fields");
    }
    {
        using C = StreamCandidate;
        const QList<C> sep{
            C{QStringLiteral("android"), 2, QStringLiteral("u3"), 100, 0, {},
              false, 720, 30, QStringLiteral("mp4")},
            C{QStringLiteral("visionos"), 0, QStringLiteral("u1"), 90, 0, {},
              false, 1080, 30, QStringLiteral("mp4")}};
        const auto ordered = orderSeparate(sep);
        CHECK(ordered.size() == 2
                  && ordered[0].client == QLatin1String("visionos")
                  && ordered[0].height == 1080,
              "visionos-first partition then score");
    }
    {
        using C = StreamCandidate;
        const QList<C> prog{C{QStringLiteral("android"), 2,
                              QStringLiteral("muxed"), 5e8, 0, {}, false, 720,
                              30, QStringLiteral("mp4")}};
        const QList<C> vid{C{QStringLiteral("visionos"), 0,
                             QStringLiteral("v"), 9e9, 0, {}, false, 1080, 30,
                             QStringLiteral("mp4")}};
        const QList<C> aud{C{QStringLiteral("visionos"), 0,
                             QStringLiteral("a"), 6e6, 0, {}, false, 0, 0,
                             QStringLiteral("m4a")}};

        auto src = buildPlaybackSource(prog, vid, aud,
                                       QStringLiteral("h.m3u8"));
        CHECK(src.has_value()
                  && src->mode == QLatin1String("adaptive_separate")
                  && !src->audioUrl.isEmpty(),
              "tier 1: separate wins with audio");
        src = buildPlaybackSource(prog, vid, {}, {});
        CHECK(src.has_value() && src->mode == QLatin1String("progressive"),
              "tier 2: progressive without audio chain");
        src = buildPlaybackSource({}, vid, {}, QStringLiteral("h.m3u8"));
        CHECK(src.has_value() && src->mode == QLatin1String("hls_last_resort"),
              "tier 3: hls before muted fallback");
        src = buildPlaybackSource({}, vid, {}, QString());
        CHECK(src.has_value()
                  && src->mode == QLatin1String("adaptive_video_only"),
              "tier 4: muted degenerate");
        CHECK(!buildPlaybackSource({}, {}, {}, QString()).has_value(),
              "nothing available -> nullopt");
    }

    {
        // hostRotationCandidates: non-googlevideo passes through; rrN---
        // prefix is renumbered and sn- token swapped per `mn` server, deduped.
        CHECK(hostRotationCandidates(QStringLiteral(
                  "https://cdn.example.com/v.mp4"))
                  == QStringList{QStringLiteral("https://cdn.example.com/v.mp4")},
              "non-googlevideo passthrough");
        const QStringList rot = hostRotationCandidates(QStringLiteral(
            "https://rr2---sn-a5s-2goe.googlevideo.com/video/0?mn="
            "sn-a5s-2goe.googlevideo.com,sn-npoe7n6s.googlevideo.com"));
        CHECK(rot.size() == 3
                  && rot.at(0).contains(QLatin1String("rr2---sn-a5s-2goe")),
              "original first");
        if (rot.size() == 3) {
            // server[0] = same sn token -> only the rr prefix renumbers to 1.
            CHECK(rot.at(1).contains(QLatin1String("rr1---sn-a5s-2goe"))
                      && !rot.at(1).contains(QLatin1String(".googlevideo.com"
                                                          ".googlevideo.com")),
                  "rr prefix replaced by index+1 (no doubled domain)");
            // server[1] = different sn token, rr renumbers to 2.
            CHECK(rot.at(2).contains(QLatin1String("rr2---sn-npoe7n6s")),
                  "sn token swapped for the alternate server");
        }
        const QStringList snRot = hostRotationCandidates(QStringLiteral(
            "https://sn-a5s-2goe.googlevideo.com/video/0?mn="
            "sn-npoe7n6s.googlevideo.com"));
        CHECK(snRot.size() == 2
                  && snRot.at(1).contains(QLatin1String("sn-npoe7n6s")),
              "sn token swapped for a bare-sn host");
    }

    std::printf(failures ? "TRAILER SUITE FAILURES=%d\n"
                         : "TRAILER SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
