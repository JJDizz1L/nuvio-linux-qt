// Offline contract for playback-session wiring. Drives a real
// StreamResolver through its PUBLIC offline ingest path - no network.
#include <nuvio/playback/PlaybackSession.h>
#include <nuvio/playback/StreamResolver.h>
#include <nuvio/p2p/P2pEngine.h>
#include <nuvio/p2p/TorrServerProcess.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QTemporaryDir>
#include <cstdio>

using nuvio::playback::PlaybackSession;
using nuvio::p2p::P2pEngine;
using nuvio::p2p::TorrServerProcess;
using nuvio::playback::StreamResolver;
static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {

struct Capture {
    int     ready            = 0;
    int     unavail          = 0;
    QString lastReadyTitle;
    QString lastReadyUrl;
    QString lastUnavailTitle;

    void attach(PlaybackSession& s)
    {
        QObject::connect(&s, &PlaybackSession::playbackReady,
                         &s, [this](const QString& t, const QString& u) {
                             ++ready;
                             lastReadyTitle = t;
                             lastReadyUrl   = u;
                         });
        QObject::connect(&s, &PlaybackSession::playbackUnavailable,
                         &s, [this](const QString& t) {
                             ++unavail;
                             lastUnavailTitle = t;
                         });
    }
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ISOLATION: the session writes the reuse-link cache on direct
    // resolutions - keep it inside a temp profile, never the live one.
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME",
            QDir(sandbox.path()).filePath("cfg").toUtf8());
    QDir().mkpath(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")));

    const QByteArray bodyDirectA =
        R"({"streams":[{"title":"1080p",)"
        R"("url":"https://cdn.example/a.mkv"}]})";
    const QByteArray bodyExternalB =
        R"({"streams":[{"title":"Ext 720p",)"
        R"("externalUrl":"https://dl.example/b.m3u8"}]})";
    const QByteArray bodyEmpty  = R"({"streams":[]})";
    const QByteArray bodyTorrent =
        R"({"streams":[{"title":"t","infoHash":"abcd"}]})";
    const QByteArray bodyDirectNew =
        R"({"streams":[{"title":"Fresh 4K",)"
        R"("url":"https://cdn.example/new.mkv"}]})";

    StreamResolver r;
    r.setAddons(QVariantList{
        QVariantMap{{"id", "alpha"}, {"name", "Alpha"},
                    {"url", "https://alpha.example/manifest.json"}},
        QVariantMap{{"id", "beta"},  {"name", "Beta"},
                    {"url", "https://beta.example/manifest.json"}},
    });

    PlaybackSession session(&r);
    Capture cap;
    cap.attach(session);

    { // cache-hit: completion happened BEFORE intent -> decide synchronously
        r.applyAddonStreams("movie/tt100", "alpha", bodyDirectA);
        r.applyAddonStreams("movie/tt100", "beta", bodyEmpty); // completes here
        CHECK(cap.ready == 0 && cap.unavail == 0,
              "no decision without user intent");

        session.requestPlay("movie", "tt100", "Cache Hit");
        CHECK(cap.ready == 1, "cache-hit decides synchronously");
        CHECK(cap.lastReadyUrl == "https://cdn.example/a.mkv",
              "first addon order wins");
        CHECK(cap.lastReadyTitle == "1080p",
              "stream title preferred over card title");
        CHECK(session.hasSession() &&
                  session.currentUrl() == cap.lastReadyUrl,
              "session properties track acceptance");

        // Repeat click on the SAME card must relaunch, not dead-end.
        session.requestPlay("movie", "tt100", "Cache Hit");
        CHECK(cap.ready == 2, "repeat click re-launches from cache");
    }

    { // in-flight path: decide only when completeness is reached
        const int readyBefore = cap.ready;
        session.requestPlay("series", "tt200", "Async");
        r.applyAddonStreams("series/tt200", "alpha", bodyEmpty);
        CHECK(cap.ready == readyBefore,
              "quiet mid-resolution (incomplete)");
        r.applyAddonStreams("series/tt200", "beta", bodyExternalB);
        CHECK(cap.ready == readyBefore + 1,
              "completing ingest decides for the pending key");
        CHECK(cap.lastReadyTitle == "Ext 720p",
              "stream title carried through (fallback unused here)");
    }

    { // honest unavailability: all addons answered, nothing directly playable
        const int readyB4 = cap.ready;
        const int unavB4  = cap.unavail;
        session.requestPlay("movie", "tt300", "Tor Only");
        r.applyAddonStreams("movie/tt300", "alpha", bodyTorrent);
        r.applyAddonStreams("movie/tt300", "beta", bodyTorrent); // completes
        CHECK(cap.ready == readyB4 && cap.unavail == unavB4 + 1,
              "torrent-only -> unavailable, never 'ready'");
        CHECK(cap.lastUnavailTitle == "Tor Only",
              "toast gets the human title");
    }

    { // stale-guard: late completion of a superseded id is dropped
        const int readyB4 = cap.ready;
        session.requestPlay("movie", "tt400", "Old");   // in flight...
        session.requestPlay("movie", "tt401", "New");   // supersedes it
        r.applyAddonStreams("movie/tt400", "alpha", bodyDirectA);
        r.applyAddonStreams("movie/tt400", "beta", bodyEmpty); // tt400 done
        CHECK(cap.ready == readyB4, "late completion of superseded id ignored");
        r.applyAddonStreams("movie/tt401", "alpha", bodyDirectNew);
        r.applyAddonStreams("movie/tt401", "beta", bodyEmpty);
        CHECK(cap.ready == readyB4 + 1 &&
                  cap.lastReadyUrl == "https://cdn.example/new.mkv",
              "current key still honored (distinct payload proves it)");
    }

    { // tier 2: torrent-only + engine attachment -> routed, not instant-toast
        qunsetenv("NUVIO_TORRSERVER_BINARY");  // force binary resolution miss
        TorrServerProcess proc("/tmp/nuvio-playback-tests/torrserver");
        P2pEngine engine(&proc);
        PlaybackSession p2session(&r, &engine);
        Capture cap3;
        cap3.attach(p2session);

        r.applyAddonStreams("movie/tt600", "alpha", bodyTorrent);
        r.applyAddonStreams("movie/tt600", "beta", bodyTorrent); // completes
        const int readyB4 = cap3.ready;
        const int unavB4  = cap3.unavail;

        p2session.requestPlay("movie", "tt600", "Via Engine");
        CHECK(cap3.ready == readyB4 && cap3.unavail == unavB4,
              "torrent-only with engine pending stays silent (not toast)");

        // Engine cannot spawn a real server in the offline suite; its
        // queued failure relays through the session as unavailable.
        const QDeadlineTimer drain(5000);
        while (!drain.hasExpired() && cap3.unavail == unavB4)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(cap3.unavail == unavB4 + 1,
              "engine failure relays as honest unavailable");
        CHECK(!p2session.hasSession(), "no session from failed engine");
    }

    { // zero addons configured: trivially complete -> immediate toast path
        StreamResolver none;                 // no addons installed
        PlaybackSession lonely(&none);
        Capture cap2;
        cap2.attach(lonely);
        lonely.requestPlay("movie", "tt500", "Nothing");
        CHECK(cap2.unavail == 1 && cap2.ready == 0,
              "no addons -> immediate honest unavailability");
        CHECK(!lonely.hasSession(), "no phantom session");
    }

    { // P3a: reuse-link fast path serves fresh cache without resolving,
      // and direct resolutions refresh the cache + parse S/E.
        using nuvio::playback::ReusePolicy;
        session.setReusePolicyProvider([] { return ReusePolicy{true, 24}; });

        // tt100 resolved directly above (cache-hit block): its link is now
        // cached. A resolver with NO data must still play from the cache.
        StreamResolver empty;
        PlaybackSession cached(&empty);
        cached.setReusePolicyProvider([] { return ReusePolicy{true, 24}; });
        Capture capReuse;
        capReuse.attach(cached);
        const int readyB4 = capReuse.ready;
        cached.requestPlay("movie", "tt100", "Cached Replay");
        CHECK(capReuse.ready == readyB4 + 1 &&
                  capReuse.lastReadyUrl == "https://cdn.example/a.mkv",
              "fresh cached link plays without resolution");
        CHECK(cached.currentSeason() == -1 && cached.currentEpisode() == -1,
              "movie session has no S/E");
        CHECK(!cached.currentIsLocalRelay(),
              "direct session is not a relay");

        // Episode-aware keys: resolve an episode, replay from a bare
        // resolver, verify S/E parse + episode-scoped cache hit.
        r.applyAddonStreams("series/tt700:1:2", "alpha", bodyDirectA);
        r.applyAddonStreams("series/tt700:1:2", "beta", bodyEmpty);
        session.requestPlay("series", "tt700:1:2", "Ep Replay");
        CHECK(session.currentSeason() == 1 && session.currentEpisode() == 2,
              "session parses composite S/E");
        PlaybackSession cachedEp(&empty);
        cachedEp.setReusePolicyProvider([] { return ReusePolicy{true, 24}; });
        Capture capEp;
        capEp.attach(cachedEp);
        cachedEp.requestPlay("series", "tt700:1:2", "Ep Replay");
        CHECK(capEp.ready == 1 &&
                  capEp.lastReadyUrl == "https://cdn.example/a.mkv",
              "episode-scoped cache hit");
        CHECK(cachedEp.currentSeason() == 1 &&
                  cachedEp.currentEpisode() == 2,
              "cached episode session parses S/E");

        // Disabled policy never consults the cache.
        PlaybackSession off(&empty);
        off.setReusePolicyProvider([] { return ReusePolicy{false, 24}; });
        Capture capOff;
        capOff.attach(off);
        off.requestPlay("movie", "tt100", "No Reuse");
        CHECK(capOff.unavail == 1 && capOff.ready == 0,
              "disabled reuse policy resolves normally (empty -> toast)");
    }

    std::printf(failures ? "SESSION SUITE FAILURES=%d\n"
                         : "SESSION SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}