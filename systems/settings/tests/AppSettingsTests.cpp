// AppSettings persistence contract: defaults, round-trips, clamps.
// ISOLATION RULE: defaultPath() honors XDG_CONFIG_HOME, so every run gets a
// fresh temp profile - these suites MUST NEVER touch the developer/user's
// real nuvio-linux config (it is live Compose-profile data).
#include <nuvio/settings/AppSettings.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <nuvio/settings/PropertiesStore.h>

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

    using namespace nuvio::settings;

    { // defaults must match the Compose line's out-of-box feel
        AppSettings s;
        CHECK(s.darkTheme() == true, "dark default");
        CHECK(s.decoderMode() == "auto", "decoder auto default");
        CHECK(s.cacheMb() == 256, "cache 256 default");
        CHECK(s.preferredAudioLanguage() == "device", "audio lang device default");
        CHECK(s.preferredSubtitleLanguage() == "none", "sub lang none default");
        CHECK(s.useForcedSubtitles() == true, "forced subs default true");
        CHECK(s.discordEnabled() == false, "discord default off");
        CHECK(s.torrentCacheSize() == "GB_2", "torrent cache GB_2 default");
    }
    {
        // SIGNALS fire once per real change, never on no-op writes.
        // Decoder note: the Compose parity contract has only three states
        // (0 device-only / 1 prefer-device / 2 prefer-app) and the bridge maps
        // 0/1 to the SAME hwdec chain — so an explicit "vaapi"/"nvdec" pick
        // degrades to "auto" on read-back BY DESIGN (documented).
        AppSettings s;
        int decoderFired = 0, themeFired = 0, cacheFired = 0;
        QObject::connect(&s, &AppSettings::decoderModeChanged,
                         [&] { ++decoderFired; });
        QObject::connect(&s, &AppSettings::darkThemeChanged,
                         [&] { ++themeFired; });
        QObject::connect(&s, &AppSettings::cacheMbChanged,
                         [&] { ++cacheFired; });

        s.setDecoderMode("software");          // parity-exact state
        s.setDecoderMode("software");          // no-op write
        s.setDarkTheme(false);
        s.setCacheMb(9999);                    // clamp high -> 2048 (GB_2)
        s.setCacheMb(10);                      // clamp low  -> 64  (MB_64)

        CHECK(decoderFired == 1, "one decoder signal");
        CHECK(themeFired == 1, "one theme signal");
        CHECK(cacheFired == 2, "clamp still counts as change");
        CHECK(s.cacheMb() == 64, "lower clamp applied");
        CHECK(s.decoderMode() == "software", "software round-trips");

        // Parity-key verification: values land in the COMPOSE stores under
        // profile-scoped keys with Compose-shaped value formats.
        PropertiesStore player{PropertiesStore::defaultPath("player_settings")};
        PropertiesStore discord{PropertiesStore::defaultPath("discord_settings")};
        const auto prio = player.getInt("decoder_priority_1");
        CHECK(prio.has_value() && *prio == 2,
              "decoder_priority_1 int parity (player_settings)");
        const auto cs = player.getString("stream_cache_size_1");
        CHECK(cs.has_value() && *cs == "MB_64",
              "stream_cache_size_1 enum-string parity");

        // Cross-instance (fresh object, same stores) read-back.
        AppSettings s2;
        CHECK(s2.decoderMode() == "software", "cross-instance decode mode");
        CHECK(s2.cacheMb() == 64, "cross-instance cache");
        CHECK(s2.darkTheme() == false, "cross-instance theme");
    }
    { // LEGACY migration: Qt-local keys port one-time into the parity keys.
        // Seed the legacy shapes exactly as the pre-P4 AppSettings wrote them,
        // then construct and verify values survive + parity keys appear.
        PropertiesStore settings{PropertiesStore::defaultPath("settings")};
        PropertiesStore torrent{PropertiesStore::defaultPath("torrent_settings")};
        settings.putString("pref_audio_lang", "pt-BR");
        settings.putString("pref_sub_lang", "en");
        settings.putBoolean("use_forced_subs", false);
        settings.putInt("stream_cache_size", 512);
        settings.putString("decoder_mode", "software");
        settings.putBoolean("discord_enabled", true);
        torrent.putString("cache_size", "GB_5");

        AppSettings s;
        CHECK(s.preferredAudioLanguage() == "pt-BR", "legacy audio migrated");
        CHECK(s.preferredSubtitleLanguage() == "en", "legacy sub migrated");
        CHECK(s.useForcedSubtitles() == false, "legacy forced subs migrated");
        CHECK(s.cacheMb() == 512, "legacy int-MB cache migrated");
        CHECK(s.decoderMode() == "software", "legacy decoder mode migrated");
        CHECK(s.discordEnabled() == true, "legacy discord migrated");
        CHECK(s.torrentCacheSize() == "GB_5", "legacy torrent cache migrated");

        // Parity stores now carry the Compose-shaped values.
        PropertiesStore player{PropertiesStore::defaultPath("player_settings")};
        PropertiesStore discord{PropertiesStore::defaultPath("discord_settings")};
        CHECK(player.getString("preferred_audio_language_1")
                  .value_or("") == "pt-BR",
              "audio parity key written");
        CHECK(player.getString("preferred_subtitle_language_1")
                  .value_or("") == "en",
              "subtitle parity key written");
        CHECK(player.getBoolean("subtitle_use_forced_subtitles_1")
                  .value_or(true) == false,
              "forced-subs parity key written");
        CHECK(player.getString("stream_cache_size_1").value_or("")
                  == "MB_512",
              "cache enum-name parity written");
        CHECK(player.getInt("decoder_priority_1").value_or(-1) == 2,
              "decoder priority parity written");
        CHECK(discord.getBoolean("discord_enabled_1").value_or(false),
              "discord parity key written");
        // NOTE: the pre-seeding `torrent` instance snapshots at construction
        // and cannot observe another instance's later writes - use a FRESH
        // store view for post-migration assertions.
        PropertiesStore torrentAfter{
            PropertiesStore::defaultPath("torrent_settings")};
        CHECK(torrentAfter.getString("cache_size_1").value_or("") == "GB_5",
              "torrent profile-scoped key written");
        CHECK(!torrentAfter.getString("cache_size").has_value(),
              "legacy torrent key removed");

        // Fresh instance reads ONLY parity now (single source of truth).
        AppSettings s2;
        CHECK(s2.preferredAudioLanguage() == "pt-BR",
              "post-migration read from parity store");
        CHECK(s2.cacheMb() == 512, "post-migration cache enum round-trip");
    }

    std::printf(failures ? "SETTINGS-APP SUITE FAILURES=%d\n"
                         : "SETTINGS-APP SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}