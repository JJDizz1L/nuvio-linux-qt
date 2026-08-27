// Offline contract for the screensaver slice: argv parity is the contract.
#include <nuvio/integrations/ScreensaverInhibit.h>

#include <QCoreApplication>
#include <cstdio>

using nuvio::integrations::ScreensaverInhibit;
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

    const auto args = ScreensaverInhibit::inhibitArgs();
    CHECK(args.size() == 5, "five argv entries");
    CHECK(args.contains("--who=nuvio-linux"), "who tagged to the app");
    CHECK(args.contains("--what=sleep:idle"),
          "sleep+idle lock (Compose chain parity)");
    CHECK(args.contains("--why=Nuvio playback"), "why text");
    CHECK(args.last() == QLatin1String("infinity"),
          "held by 'sleep infinity' child: kill == unlock");

    std::printf(failures ? "SSAVER SUITE FAILURES=%d\n"
                         : "SSAVER SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}
