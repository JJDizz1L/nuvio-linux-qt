// Tracking T1 contract: media mapping, registry fan-out, pump edges.
// No network, no profile stores.
#include <nuvio/tracking/ScrobblePump.h>
#include <nuvio/tracking/TrackingRegistry.h>
#include <nuvio/tracking/TrackingTypes.h>

#include <QCoreApplication>

#include <cstdio>

using nuvio::tracking::ScrobbleAction;
using nuvio::tracking::ScrobbleEvent;
using nuvio::tracking::ScrobblePump;
using nuvio::tracking::SeekScrobblePolicy;
using nuvio::tracking::TrackingMediaKind;
using nuvio::tracking::TrackingProvider;
using nuvio::tracking::TrackingRegistry;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

namespace {
struct Call {
    TrackingProvider provider;
    ScrobbleAction action;
    double progress = 0.0;
};
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace nuvio::tracking;

    { // media mapping from playback identities
        const auto ep = mediaForPlayback("series", "tt123:2:4", "Ep");
        CHECK(ep.kind == TrackingMediaKind::Show &&
                  ep.ids.imdb == "tt123" && ep.episode.season == 2 &&
                  ep.episode.number == 4,
              "composite series maps to show+episode media");
        const auto an = mediaForPlayback("anime", "mal:52034", "A");
        CHECK(an.kind == TrackingMediaKind::Anime && an.ids.mal == 52034 &&
                  an.episode.number < 0,
              "mal id maps to anime without episode coordinate");
        const auto mv = mediaForPlayback("movie", "tt999", "M");
        CHECK(mv.kind == TrackingMediaKind::Movie && mv.ids.imdb == "tt999",
              "movie maps with imdb id");
        CHECK(scrobbleWireValue(ScrobbleAction::Start) == "start" &&
                  scrobbleWireValue(ScrobbleAction::Pause) == "pause" &&
                  scrobbleWireValue(ScrobbleAction::Stop) == "stop",
              "wire values");
    }

    { // registry: connected-only fan-out + seek-policy filter
        TrackingRegistry reg;
        QList<Call> calls;
        reg.registerScrobbler(TrackingProvider::Trakt,
                              SeekScrobblePolicy::StopAndRestart,
                              [&](int, ScrobbleAction a, const ScrobbleEvent& e) {
                                  calls.append({TrackingProvider::Trakt, a,
                                                e.progressPercent});
                                  return true;
                              });
        reg.registerScrobbler(TrackingProvider::Simkl,
                              SeekScrobblePolicy::None,
                              [&](int, ScrobbleAction a, const ScrobbleEvent&) {
                                  calls.append({TrackingProvider::Simkl, a,
                                                0.0});
                                  return false;   // failure collected
                              });
        // Nobody connected: silent.
        CHECK(ScrobbleCoordinator::scrobble(reg, 1, 1,
                                            ScrobbleAction::Start, {})
                      .isEmpty() &&
                  calls.isEmpty(),
              "disconnected providers never run");
        // Profile guard: wrong profile short-circuits.
        reg.setProviderAuthenticated(TrackingProvider::Trakt, true);
        reg.setProviderAuthenticated(TrackingProvider::Simkl, true);
        CHECK(ScrobbleCoordinator::scrobble(reg, 1, 2,
                                            ScrobbleAction::Start, {})
                      .isEmpty() &&
                  calls.isEmpty(),
              "foreign profile short-circuits");
        ScrobbleEvent ev;
        ev.progressPercent = 10.0;
        const auto failuresOut = ScrobbleCoordinator::scrobble(
            reg, 1, 1, ScrobbleAction::Start, ev);
        CHECK(calls.size() == 2 &&
                  failuresOut.size() == 1 &&
                  failuresOut[0].provider == TrackingProvider::Simkl,
              "fan-out runs connected, collects failures");
        calls.clear();
        ScrobbleCoordinator::scrobbleSeek(reg, 1, 1, ScrobbleAction::Stop,
                                          ev);
        CHECK(calls.size() == 1 &&
                  calls[0].provider == TrackingProvider::Trakt,
              "seek path runs restart-policy providers only");
        CHECK(reg.connectedProviderIds().size() == 2,
              "connected set tracks auth");
        reg.setProviderAuthenticated(TrackingProvider::Simkl, false);
        CHECK(reg.connectedProviderIds().size() == 1,
              "disconnect shrinks the set");
    }

    { // pump edges: start once, pause edge, 80% stop, seek restart
        TrackingRegistry reg;
        QList<Call> calls;
        reg.registerScrobbler(TrackingProvider::Trakt,
                              SeekScrobblePolicy::StopAndRestart,
                              [&](int, ScrobbleAction a, const ScrobbleEvent& e) {
                                  calls.append({TrackingProvider::Trakt, a,
                                                e.progressPercent});
                                  return true;
                              });
        reg.setProviderAuthenticated(TrackingProvider::Trakt, true);
        ScrobblePump pump(&reg);
        pump.beginItem("movie", "tt1", "M");
        pump.tick(1000, 100000, false);   // 1%
        pump.tick(2000, 100000, false);
        CHECK(calls.size() == 1 && calls[0].action == ScrobbleAction::Start,
              "start fires once");
        pump.tick(3000, 100000, true);    // pause rising edge
        pump.tick(3000, 100000, true);    // held pause: no repeat
        CHECK(calls.size() == 2 && calls[1].action == ScrobbleAction::Pause,
              "pause fires on rising edge only");
        pump.tick(3000, 100000, false);
        pump.tick(85000, 100000, false);  // jump: seek-stop, restart start,
                                          // then 85% completion stop
        CHECK(calls.size() == 5, "seek-stop + restart + completion dispatched");
        pump.tick(86000, 100000, false);
        CHECK(calls.size() == 5, "completion stop fires once");
        // New item resets every latch.
        pump.beginItem("movie", "tt2", "M2");
        pump.tick(1000, 100000, false);
        CHECK(calls.size() == 6 && calls[5].action == ScrobbleAction::Start,
              "new item re-arms start");
    }

    std::printf(failures ? "TRACKING SUITE FAILURES=%d\n"
                         : "TRACKING SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
