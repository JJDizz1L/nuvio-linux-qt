// Headless suite for the screen-stack contract. No QGuiApplication: signals
// are direct connections, so a bare instantiation exercises everything.
#include <nuvio/ui/NavigationModel.h>

#include <cstdio>

using nuvio::ui::NavigationModel;
static int failures = 0;
#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++failures;                                                     \
            std::fprintf(stderr, "FAIL %s\n", msg);                         \
        }                                                                   \
    } while (0)

int main()
{
    { // seeding + clamping
        NavigationModel nav;
        CHECK(nav.depth() == 1, "seeded depth 1");
        CHECK(nav.currentRoute() == "home", "seeded route home");
        nav.pop();                       // must clamp, not underflow
        CHECK(nav.depth() == 1, "pop at root clamps");
        CHECK(!nav.canPop(), "cannot pop root");
        nav.popToRoot();
        CHECK(nav.currentRoute() == "home", "popToRoot keeps home");
    }
    { // push/pop mechanics + double-click guard
        NavigationModel nav;
        nav.push("video");
        CHECK(nav.currentRoute() == "video", "push video");
        CHECK(nav.canPop(), "can pop after push");
        const bool ignored = nav.pushIfDifferent("video");   // same top
        CHECK(!ignored && nav.depth() == 2, "duplicate push no-op");
        nav.push("library");
        CHECK(nav.depth() == 3 && nav.currentRoute() == "library",
              "A,B,A-style stack legal");
        nav.pop();
        CHECK(nav.currentRoute() == "video", "pop returns to video");
        nav.pop();
        nav.pop();
        CHECK(nav.currentRoute() == "home" && nav.depth() == 1,
              "pops back to seeded root");
    }
    { // replaceTop semantics (deep-link launches)
        NavigationModel nav;
        nav.replaceTop("library");
        CHECK(nav.currentRoute() == "library", "replaceTop on seed");
        nav.push("video");
        nav.replaceTop("settings-stub");
        CHECK(nav.currentRoute() == "settings-stub" && nav.depth() == 2,
              "replaceTop swaps only the top");
        CHECK(!nav.pushIfDifferent(""), "empty-route push rejected");
    }
    { // stateChanged fires only on real mutations
        NavigationModel nav;
        int fired = 0;
        QObject::connect(&nav, &NavigationModel::stateChanged,
                         [&fired] { ++fired; });
        nav.pushIfDifferent("home");     // duplicate -> silent
        nav.pop();                       // at root      -> silent
        nav.replaceTop("home");          // identical    -> silent
        CHECK(fired == 0, "no-op transitions stay quiet");
        nav.push("library");
        nav.pop();
        CHECK(fired == 2, "real transitions notify once each");
    }

    std::printf(failures ? "NAV SUITE FAILURES=%d\n"
                         : "NAV SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}