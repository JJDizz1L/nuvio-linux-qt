// AppSettings persistence contract: defaults, round-trips, clamps.
// ISOLATION RULE: defaultPath() honors XDG_CONFIG_HOME, so every run gets a
// fresh temp profile - these suites MUST NEVER touch the developer/user's
// real nuvio-linux config (it is live Compose-profile data).
#include <nuvio/settings/AppSettings.h>

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

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);      // applicationDirPath needs it

    // Redirect the whole profile root into an auto-removed temp dir.
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) { std::fprintf(stderr, "FAIL sandbox\n"); return 2; }
    const QByteArray cfgHome =
        QDir(sandbox.path()).filePath("config").toUtf8();
    qputenv("XDG_CONFIG_HOME", cfgHome);
    QDir().mkpath(QString::fromUtf8(cfgHome));

    using nuvio::settings::AppSettings;

    { // defaults must match the Compose line's out-of-box feel
        AppSettings s;
        CHECK(s.darkTheme() == true, "dark default");
        CHECK(s.decoderMode() == "auto", "decoder auto default");
        CHECK(s.cacheMb() == 256, "cache 256 default");
    }
    { // signals fire once per real change, never on no-op writes
        AppSettings s;
        int decoderFired = 0, themeFired = 0, cacheFired = 0;
        QObject::connect(&s, &AppSettings::decoderModeChanged,
                         [&] { ++decoderFired; });
        QObject::connect(&s, &AppSettings::darkThemeChanged,
                         [&] { ++themeFired; });
        QObject::connect(&s, &AppSettings::cacheMbChanged,
                         [&] { ++cacheFired; });

        s.setDecoderMode("vaapi");
        s.setDecoderMode("vaapi");            // no-op write
        s.setDarkTheme(false);
        s.setCacheMb(9999);                   // clamp high -> 2048
        s.setCacheMb(10);                     // clamp low  -> 64

        CHECK(decoderFired == 1, "one decoder signal");
        CHECK(themeFired == 1, "one theme signal");
        CHECK(cacheFired == 2, "clamp still counts as change");
        CHECK(s.cacheMb() == 64, "lower clamp applied");

        AppSettings s2;                       // fresh instance, same store:
        CHECK(s2.decoderMode() == "vaapi", "cross-instance read-back");
    }

    std::printf(failures ? "SETTINGS-APP SUITE FAILURES=%d\n"
                         : "SETTINGS-APP SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}