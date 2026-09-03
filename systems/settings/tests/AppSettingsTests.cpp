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
        // P1a parity fix: Compose SubtitleStyleState.DEFAULT.useForcedSubtitles
        // is FALSE (both live shared files store an explicit true instead).
        CHECK(s.useForcedSubtitles() == false, "forced subs default false");
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
    { // P1a: Compose UiState/DEFAULT values for every new key.
        AppSettings s;
        CHECK(s.showLoadingOverlay() == true, "overlay default");
        CHECK(s.showParentalGuide() == true, "parental default");
        CHECK(s.resizeMode() == "Fit", "resize Fit default");
        CHECK(s.holdToSpeedEnabled() == true, "hold-speed default");
        CHECK(s.holdToSpeedValue() == 2.0f, "hold-speed 2x default");
        CHECK(s.touchGesturesEnabled() == true, "gestures default");
        CHECK(s.mapDv7ToHevc() == false, "dv7 default");
        CHECK(s.tunnelingEnabled() == false, "tunneling default");
        CHECK(s.useLibass() == false, "libass default");
        CHECK(s.libassRenderType() == "CUES", "libass render default");
        CHECK(s.nvidiaRtxSuperResolutionEnabled() == false, "rtx default");
        CHECK(s.externalPlayerEnabled() == false, "ext player default");
        CHECK(s.externalPlayerForwardSubtitles() == false, "ext fwd default");
        CHECK(s.externalPlayerSendSkipSegments() == false, "ext skip default");
        CHECK(s.externalPlayerId() == "system", "ext id system default");
        CHECK(s.secondaryPreferredAudioLanguage() == "", "sec audio unset");
        CHECK(s.secondaryPreferredSubtitleLanguage() == "", "sec sub unset");
        CHECK(s.subtitleBackgroundColor() == "#00000000", "sub bg default");
        CHECK(s.subtitleOutlineColor() == "#FF000000", "sub outline default");
        CHECK(s.subtitleStripSdh() == false, "sdh default");
        CHECK(s.subtitleShowOnlyPreferredLanguages() == false,
              "show-only default");
        CHECK(s.addonSubtitleStartupMode() == "FAST_STARTUP",
              "startup mode default");
        CHECK(s.streamReuseLastLinkEnabled() == false, "reuse default");
        CHECK(s.streamReuseLastLinkCacheHours() == 24, "reuse hours default");
        CHECK(s.streamAutoPlaySource() == "ALL_SOURCES", "ap source default");
        CHECK(s.streamAutoPlaySelectedAddons().isEmpty(), "ap addons empty");
        CHECK(s.streamAutoPlaySelectedPlugins().isEmpty(), "ap plugins empty");
        CHECK(s.skipIntroEnabled() == true, "skip intro default");
        CHECK(s.autoSkipSegmentTypes().isEmpty(), "skip types empty");
        CHECK(s.animeSkipEnabled() == false, "animeskip default");
        CHECK(s.animeSkipClientId() == "", "animeskip id default");
        CHECK(s.introDbApiKey() == "", "introdb default");
        CHECK(s.introSubmitEnabled() == false, "intro submit default");
        CHECK(s.streamAutoPlayNextEpisodeEnabled() == false,
              "next-ep default");
        CHECK(s.streamAutoPlayNextEpisodeFallbackEnabled() == true,
              "next-ep fallback default");
        CHECK(s.streamAutoPlayPreferBingeGroup() == true, "binge default");
        CHECK(s.streamAutoPlayReuseBingeGroup() == true, "reuse binge default");
        CHECK(s.nextEpisodeThresholdMode() == "PERCENTAGE",
              "threshold mode default");
        CHECK(s.nextEpisodeThresholdPercent() == 99.0f, "threshold pct default");
        CHECK(s.nextEpisodeThresholdMinutesBeforeEnd() == 2.0f,
              "threshold min default");
    }
    { // P1a: round-trips, enum validation, set sorting, parity-key spellings.
        AppSettings s;
        int optsFired = 0, styleFired = 0, apFired = 0;
        QObject::connect(&s, &AppSettings::playerOptionsChanged,
                         [&] { ++optsFired; });
        QObject::connect(&s, &AppSettings::subtitleStyleChanged,
                         [&] { ++styleFired; });
        QObject::connect(&s, &AppSettings::streamAutoPlayChanged,
                         [&] { ++apFired; });

        s.setResizeMode("Fill");
        s.setResizeMode("Sideways");   // invalid: rejected, no signal
        s.setStreamAutoPlaySource("ENABLED_PLUGINS_ONLY");
        s.setNextEpisodeThresholdMode("MINUTES_BEFORE_END");
        s.setNextEpisodeThresholdMode("BOGUS");  // rejected
        s.setAutoSkipSegmentTypes(QStringList{"recap", "intro"}); // sorted
        s.setSecondaryPreferredAudioLanguage("en");
        s.setSecondaryPreferredAudioLanguage("");  // unsets (Compose null)
        s.setHoldToSpeedValue(9.0f);   // clamp high -> 4.0
        s.setStreamAutoPlayTimeoutSeconds(-4);  // Compose: negatives -> 0
        s.setSubtitleBackgroundColor("#80000000");
        s.setSubtitleShowOnlyPreferredLanguages(true);

        CHECK(s.resizeMode() == "Fill", "resize round-trip");
        CHECK(s.streamAutoPlaySource() == "ENABLED_PLUGINS_ONLY",
              "new source value round-trips");
        CHECK(s.nextEpisodeThresholdMode() == "MINUTES_BEFORE_END",
              "threshold mode round-trip");
        CHECK((s.autoSkipSegmentTypes() == QStringList{"intro", "recap"}),
              "segment types sorted on write");
        CHECK(s.secondaryPreferredAudioLanguage() == "",
              "secondary empty after unset");
        CHECK(s.holdToSpeedValue() == 4.0f, "hold-speed clamp");
        CHECK(s.streamAutoPlayTimeoutSeconds() == 0, "negative timeout -> 0");
        CHECK(optsFired == 4, "one options signal per real change");
        CHECK(styleFired == 2, "subtitle signals for bg + show-only");
        CHECK(apFired == 2, "autoplay signals for source + timeout");

        PropertiesStore player{PropertiesStore::defaultPath("player_settings")};
        CHECK(player.getString("resize_mode_1").value_or("") == "Fill",
              "resize parity key");
        CHECK(player.getString("stream_auto_play_source_1").value_or("")
                  == "ENABLED_PLUGINS_ONLY",
              "source parity key");
        CHECK(player.getString("auto_skip_segment_types_1").value_or("")
                  == "[\"intro\",\"recap\"]",
              "string-set parity shape (sorted kotlinx array)");
        CHECK(!player.getString("secondary_preferred_audio_language_1")
                   .has_value(),
              "secondary unset removes the key");
        CHECK(player.getString("subtitle_background_color_1").value_or("")
                  == "#80000000",
              "sub bg parity key");
        CHECK(player.getInt("stream_auto_play_timeout_seconds_1")
                      .value_or(-1) == 0,
              "timeout parity key");

        // Compose-shaped cross-read: a fork-written row decodes exactly.
        {
            PropertiesStore seed{
                PropertiesStore::defaultPath("player_settings")};
            seed.putString("next_episode_threshold_mode_1",
                           "MINUTES_BEFORE_END");
            seed.putFloat("next_episode_threshold_percent_v2_1", 80.5f);
            seed.putBoolean("stream_auto_play_prefer_binge_group_1", false);
            seed.putString("external_player_id_1", "mpv");
        }
        AppSettings s2;
        CHECK(s2.nextEpisodeThresholdMode() == "MINUTES_BEFORE_END",
              "cross-read threshold mode");
        CHECK(s2.nextEpisodeThresholdPercent() == 80.5f,
              "cross-read threshold pct");
        CHECK(s2.streamAutoPlayPreferBingeGroup() == false,
              "cross-read binge flag");
        CHECK(s2.externalPlayerId() == "mpv", "cross-read ext id");
    }
    { // QML-facing property registration: methods WITHOUT Q_PROPERTY are
      // invisible to QML (reads yield undefined -> dead bindings). Pin the
      // subtitle-appearance registrations (the 2026-08-27 bug class).
        const QMetaObject* mo = &AppSettings::staticMetaObject;
        CHECK(mo->indexOfProperty("subtitleFontSize") >= 0,
              "subtitleFontSize registered");
        CHECK(mo->indexOfProperty("subtitleTextColor") >= 0,
              "subtitleTextColor registered");
        CHECK(mo->indexOfProperty("subtitleOutlineEnabled") >= 0,
              "subtitleOutlineEnabled registered");
        CHECK(mo->indexOfProperty("subtitleOutlineWidth") >= 0,
              "subtitleOutlineWidth registered");
        CHECK(mo->indexOfProperty("subtitleBold") >= 0,
              "subtitleBold registered");
        CHECK(mo->indexOfProperty("subtitleBottomOffset") >= 0,
              "subtitleBottomOffset registered");
        CHECK(mo->indexOfProperty("subtitleBackgroundColor") >= 0,
              "subtitleBackgroundColor registered");
        CHECK(mo->indexOfProperty("subtitleOutlineColor") >= 0,
              "subtitleOutlineColor registered");
        CHECK(mo->indexOfProperty("resizeMode") >= 0,
              "resizeMode registered");
        CHECK(mo->indexOfProperty("streamAutoPlaySource") >= 0,
              "streamAutoPlaySource registered");
        CHECK(mo->indexOfProperty("skipIntroEnabled") >= 0,
              "skipIntroEnabled registered");
        CHECK(mo->indexOfProperty("nextEpisodeThresholdMode") >= 0,
              "nextEpisodeThresholdMode registered");
    }

    std::printf(failures ? "SETTINGS-APP SUITE FAILURES=%d\n"
                         : "SETTINGS-APP SUITE OK (%d failures)\n",
                failures);
    return failures ? 1 : 0;
}