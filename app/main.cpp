#include <QCommandLineParser>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <algorithm>

#include <clocale>
#include <cstdio>
#include <memory>

#include <QLocale>

#include <mpv/client.h>

#include "bootstrap/LogCategories.h"
#include "bootstrap/ModuleRegistry.h"
#include "bootstrap/SmokeRunner.h"
#include "Version.h"                    // generated via configure_file
#include "nuvio/integrations/DiscordRpc.h"
#include "nuvio/integrations/MprisService.h"
#include "nuvio/integrations/ScreensaverInhibit.h"
#include "nuvio/mpv/MpvController.h"
#include "nuvio/mpv/MpvLog.h"
#include "nuvio/mpv/MpvQuickItem.h"
#include "nuvio/mpv/TrackAutoSelector.h"
#include "nuvio/authsync/AddonsSyncController.h"
#include "nuvio/authsync/CollectionSyncController.h"
#include "nuvio/authsync/HomeCatalogSyncController.h"
#include "nuvio/authsync/LibrarySyncController.h"
#include "nuvio/authsync/ProviderCredsController.h"
#include "nuvio/debrid/CloudLibrary.h"
#include "nuvio/debrid/DebridAuth.h"
#include "nuvio/debrid/DebridResolver.h"
#include "nuvio/debrid/DebridSettings.h"
#include "nuvio/downloads/DownloadManager.h"
#include "nuvio/mdblist/MdbListService.h"
#include "nuvio/mdblist/MdbListSettings.h"
#include "nuvio/membership/CommunityService.h"
#include "nuvio/membership/MemberAccess.h"
#include "nuvio/membership/MembershipOverview.h"
#include "nuvio/notifications/ReleaseNotifications.h"
#include "nuvio/plugins/PluginRepository.h"
#include "nuvio/tmdb/TmdbService.h"
#include "nuvio/tmdb/TmdbSettings.h"
#include "nuvio/authsync/ProgressSyncController.h"
#include "nuvio/authsync/AuthService.h"
#include "nuvio/authsync/SyncOrchestrator.h"
#include "nuvio/profiles/ProfileManager.h"
#include "nuvio/tracking/ScrobblePump.h"
#include "nuvio/tracking/SimklAuth.h"
#include "nuvio/tracking/SimklScrobble.h"
#include "nuvio/tracking/TrackingRegistry.h"
#include "nuvio/tracking/TraktAuth.h"
#include "nuvio/tracking/TraktScrobble.h"
#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/library/AddonRegistry.h"
#include "nuvio/library/CollectionStore.h"
#include "nuvio/library/HomeShelves.h"
#include "nuvio/library/LibraryStore.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SearchHistory.h"
#include "nuvio/settings/SyncIdentity.h"
#include "nuvio/library/CatalogService.h"
#include "nuvio/library/MetaService.h"
#include "nuvio/playback/PlaybackSession.h"
#include "nuvio/playback/NextEpisodeHelper.h"
#include "nuvio/playback/ParentalGuide.h"
#include "nuvio/playback/SkipResolver.h"
#include "nuvio/playback/StreamResolver.h"
#include "nuvio/p2p/P2pEngine.h"
#include "nuvio/p2p/TorrServerProcess.h"
#include "nuvio/platform/XdgPaths.h"
#include "nuvio/ui/NavigationModel.h"
#include "nuvio/ui/PreferencesApplier.h"
#include "nuvio/settings/AppSettings.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/trailer/TrailerResolver.h"
#include "nuvio/watching/WatchRecorder.h"
#include "nuvio/watching/WatchingStore.h"
#include "nuvio/ui/PosterProvider.h"
#include "nuvio/ui/UiBootstrap.h"

int main(int argc, char* argv[])
{
    // X11/Wayland agnostic: platform plugins are Qt's business. Explicit
    // share-context attribute enables future multi-window/multi-surface.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QGuiApplication app(argc, argv);

    // PORTABILITY-CRITICAL: QGuiApplication internally runs
    // setlocale(LC_ALL, "") which resets LC_NUMERIC to the user locale;
    // libmpv REQUIRES LC_NUMERIC == "C" or mpv_create() returns null
    // (probe-reproduced independently of our app — see plan P0 notes).
    std::setlocale(LC_NUMERIC, "C");
    std::fprintf(stderr, "NUVIO-BOOT: entered main\n");   // unconditional truth

    // Compose-parity typography: bundled JetBrains Sans as the application
    // font, before any QML consumes the default font. One boot-truth line
    // records the outcome (font stack defects are screenshot-visible).
    const QString appFontFamily = nuvio::ui::loadBundledFonts();
    std::fprintf(stderr, "NUVIO-BOOT: font=%s\n",
                 appFontFamily.isEmpty() ? "<platform-default>"
                                         : appFontFamily.toUtf8().constData());

    QGuiApplication::setApplicationName(QStringLiteral("nuvio-linux"));
    QGuiApplication::setApplicationVersion(QStringLiteral(NUVIO_VERSION_STRING));
    QGuiApplication::setOrganizationDomain(QStringLiteral("io.github.jdizz1l"));
    // Wayland app_id / Hyprland class = package name (nuvio-linux),
    // identical dev vs packaged by construction. Matches omarchy video_class
    // lua rules once extended with this literal.
    QGuiApplication::setDesktopFileName(QStringLiteral("nuvio-linux"));

    nuvio::mpv::applyMpvDebugEnv();

    QCommandLineParser cli;
    cli.setApplicationDescription(
        QStringLiteral("Nuvio Linux — Qt exploration line (P0 structure)"));
    cli.addHelpOption();
    const QCommandLineOption dumpModules(
        QStringLiteral("dump-modules"),
        QStringLiteral("Print built systems/modules table and exit"));
    cli.addOption(dumpModules);
    cli.addPositionalArgument(
        QStringLiteral("media"),
        QStringLiteral("File or stream URL to play on launch"),
        QStringLiteral("[media]"));
    cli.process(app);

    if (cli.isSet(dumpModules)) {
        qCInfo(lcNuvioAppModules).noquote()
            << QStringLiteral("Nuvio Linux modules");
        for (const auto& m : nuvio::app::modules())
            qCInfo(lcNuvioAppModules).nospace().noquote()
                << "  " << m.name.leftJustified(14) << ' ' << m.status;
        return 0;
    }

    // Deterministic look across desktop environments (KDE would otherwise
    // apply Fusion); Basic ships inside qt6-declarative everywhere.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QStringList positional = cli.positionalArguments();
    const QString mediaUrl = positional.isEmpty() ? QString{}
                                                  : positional.first();

    nuvio::app::SmokeRunner::Config smokeCfg;
    const bool smoke = nuvio::app::SmokeRunner::requested(&smokeCfg);
    const QString launchUrl =
        !mediaUrl.isEmpty() ? mediaUrl
                            : (smoke ? smokeCfg.url : QString());

    // ---- explicit wiring (anti-god-object: everything visible here) -------
    auto controller = std::make_unique<nuvio::mpv::MpvController>();
    controller->start();

    // Screen-stack viewmodel + async poster pipeline (Phase 3 skeleton).
    // Declared after controller so teardown order unwinds nav before the
    // mpv core joins (QML is already dead by then either way).
    auto navigation = std::make_unique<nuvio::ui::NavigationModel>();
    auto auth       = std::make_unique<nuvio::authsync::AuthService>();

    // Device identity for future profile sync (Compose SyncClientIdentity
    // parity): ensure a valid client_instance_id exists before any auth or
    // sync traffic. First run generates; later runs adopt.
    {
        nuvio::settings::PropertiesStore idStore(
            nuvio::settings::PropertiesStore::defaultPath("sync_client_identity"));
        const QString clientId =
            nuvio::settings::SyncIdentity::currentClientId(idStore);
        qCInfo(lcNuvioAppModules).noquote()
            << "sync identity ready:" << clientId;
    }

    auto settings   = std::make_unique<nuvio::settings::AppSettings>();
    auto catalog    = std::make_unique<nuvio::library::CatalogService>();
    auto metaSvc    = std::make_unique<nuvio::library::MetaService>();
    auto addonreg   = std::make_unique<nuvio::library::AddonRegistry>();
    addonreg->load();

    // Streams resolve against the user-installed addon set; kept in sync
    // whenever the registry changes (install/remove).
    auto streamResolver =
        std::make_unique<nuvio::playback::StreamResolver>();
    const auto syncResolverAddons = [streamResolverPtr = streamResolver.get(),
                                     registryPtr = addonreg.get()] {
        QVariantList list;
        for (const auto& a : registryPtr->addons()) {
            const auto m      = a.toMap();
            // Compose parity: disabled addons contribute no streams.
            if (m.value(QStringLiteral("enabled"), true) == false) continue;
            const QString url = m.value("url").toString();
            if (url.isEmpty()) continue;
            list.append(QVariantMap{
                {"id",   m.value("id").toString()},
                {"name", m.value("name").toString()},
                {"url",  url}});
        }
        streamResolverPtr->setAddons(list);
    };
    syncResolverAddons();
    QObject::connect(addonreg.get(),
                     &nuvio::library::AddonRegistry::changed,
                     syncResolverAddons);
    QObject::connect(
        streamResolver.get(),
        &nuvio::playback::StreamResolver::resolutionComplete,
        [](const QString& type, const QString& imdbId,
           const QVariantMap& best) {
            if (best.isEmpty())
                std::fprintf(stderr,
                             "stream: %s/%s -> no direct source "
                             "(torrent-only or unreachable)\n",
                             qPrintable(type), qPrintable(imdbId));
            else
                std::fprintf(stderr, "stream: %s/%s -> %s [%s]\n",
                             qPrintable(type), qPrintable(imdbId),
                             qPrintable(best.value("url").toString()),
                             qPrintable(best.value("source").toString()));
        });
    // Explicit track selection (NO alang/slang — AGENTS.md masked-failure
    // rule): policy runs on FILE_LOADED with applied-latches per tier; the
    // controller's queued command path executes the aid/sid sets.
    auto trackSelector = std::make_unique<nuvio::mpv::TrackAutoSelector>();
    QObject::connect(controller.get(), &nuvio::mpv::MpvController::fileLoaded,
                     trackSelector.get(),
                     &nuvio::mpv::TrackAutoSelector::handleFileLoaded);
    QObject::connect(
        controller.get(), &nuvio::mpv::MpvController::trackListChanged,
        trackSelector.get(), &nuvio::mpv::TrackAutoSelector::handleTracks);
    trackSelector->setPreferencesProvider([sp = settings.get()] {
        nuvio::mpv::tracksel::LanguagePrefs p;
        p.preferredAudio      = sp->preferredAudioLanguage();
        p.preferredSubtitle   = sp->preferredSubtitleLanguage();
        // Device-language leg of the target chain (Compose's
        // DeviceLanguagePreferences expect-actual); most preferred first.
        for (const QString& lang : QLocale::system().uiLanguages())
            p.deviceLanguages.append(lang);
        return p;
    });
    QObject::connect(trackSelector.get(),
                     &nuvio::mpv::TrackAutoSelector::commandReady,
                     controller.get(), [cp = controller.get()](QStringList a) {
                         cp->enqueueCommand(a);
                     });
    auth->restoreSession();
    catalog->loadShelves();     // stored tokens -> active or silent refresh

    // Hero ambient trailer (Compose detail-hero parity): a SECOND mpv
    // instance dedicated to the MetaPage backdrop. Per-instance controllers
    // are the sanctioned pattern (Compose runs several concurrent surfaces);
    // this one is born muted so it can never touch the user's volume.
    // Kill switch: NUVIO_NO_HERO=1 skips instance creation entirely.
    std::unique_ptr<nuvio::mpv::MpvController> heroController;
    const bool heroEnabled = !qEnvironmentVariableIsSet("NUVIO_NO_HERO");
    if (heroEnabled) {
        heroController = std::make_unique<nuvio::mpv::MpvController>();
        heroController->start();
        heroController->setVolumePercent(0);   // ambient: born muted
    }

    // Watch-progress sync (sync-breadth leg): dirty pushes on recorder
    // signals + startup full/delta pull. Self-guards when signed out.
    auto progressSync =
        std::make_unique<nuvio::authsync::ProgressSyncController>(
            nuvio::authsync::AuthConfig::load(),
            [ap = auth.get()] { return ap->accessToken(); },
            nuvio::settings::ActiveProfile::id());
    progressSync->setDebounceMs(1500);

    // Profile-settings sync (P4 leg 4): background startup pull + debounced
    // push on settings changes. Fully self-guarding: signed-out or
    // unconfigured endpoints make every operation a silent no-op.
    auto syncOrch =
        std::make_unique<nuvio::authsync::SyncOrchestrator>(
            settings.get(), nuvio::authsync::AuthConfig::load(),
            [ap = auth.get()] { return ap->accessToken(); });
    syncOrch->beginObserving();
    syncOrch->pullNow();

    // Discord Rich Presence: opt-in, live-togglable. Content = media title
    // with play timestamps (Compose parity: paused drops the clock, seek >
    // 4s rebuilds, 800ms debounce coalesces position chatter).
    auto discord = std::make_unique<nuvio::integrations::DiscordPresence>();
    discord->setClientId(qEnvironmentVariable("NUVIO_DISCORD_CLIENT_ID"));
    QObject::connect(settings.get(),
                     &nuvio::settings::AppSettings::discordEnabledChanged,
                     discord.get(), [dp = discord.get(), sp = settings.get()] {
                         if (sp->discordEnabled())
                             QMetaObject::invokeMethod(dp, "connectNow",
                                                       Qt::QueuedConnection);
                         else
                             dp->stop();
                     });
    QObject::connect(controller.get(), &nuvio::mpv::MpvController::snapshotChanged,
                     discord.get(), [dp = discord.get()](nuvio::mpv::PlaybackSnapshot s) {
                         if (!s.hasMedia()) return;   // browse stays silent
                         dp->updateProgress(s.positionSec < 0 ? -1.0 : s.positionSec,
                                            s.durationSec,
                                            s.paused);
                     });
    if (settings->discordEnabled()) {
        QMetaObject::invokeMethod(discord.get(), "connectNow",
                                  Qt::QueuedConnection);
    }

    // Session wiring: library card intent -> resolver outcome -> player
    // route. Owns pending-intent state so stale completions never launch
    // the wrong title (see PlaybackSession.h). Torrent-only resolutions
    // route through the local TorrServer engine; its binary is resolved
    // from NUVIO_TORRSERVER_BINARY / dev-tree / vendor paths at start
    // time - absence surfaces as an honest failure (toast), never a
    // phantom playable.
    auto torrserverProcess =
        std::make_unique<nuvio::p2p::TorrServerProcess>(
            nuvio::platform::appCacheDir()
                + QStringLiteral("/torrserver"));
    auto p2pEngine = std::make_unique<nuvio::p2p::P2pEngine>(
        torrserverProcess.get());
    // Cache-size setting reaches /settings on every fresh binary start,
    // BEFORE any torrent is added (store-without-send was a real bug in
    // the Compose line - never repeat it).
    p2pEngine->setCacheSizeProvider([sp = settings.get()] {
        return nuvio::p2p::toTorrServerCacheMb(sp->torrentCacheSize());
    });
    auto playbackSession =
        std::make_unique<nuvio::playback::PlaybackSession>(
            streamResolver.get(), p2pEngine.get());
    // Reuse-link fast path (P3a): fresh cached direct links skip
    // resolution; direct resolutions refresh the cache in-session.
    playbackSession->setReusePolicyProvider([sp = settings.get()] {
        nuvio::playback::ReusePolicy p;
        p.enabled = sp->streamReuseLastLinkEnabled();
        p.cacheHours = sp->streamReuseLastLinkCacheHours();
        return p;
    });
    // MPRIS: desktop media keys / playerctl / shell widgets drive the same
    // queued command surface as everything else (new capability, plan P3).
    auto mpris = std::make_unique<nuvio::integrations::MprisService>(
        controller.get(), playbackSession.get());
    mpris->start();

    // Screensaver inhibit tracks the PLAYING state (parity: acquire on
    // media+unpaused, release on pause/end/teardown).
    auto screensaver = std::make_unique<nuvio::integrations::ScreensaverInhibit>();
    QObject::connect(controller.get(), &nuvio::mpv::MpvController::snapshotChanged,
                     screensaver.get(), [ss = screensaver.get()](nuvio::mpv::PlaybackSnapshot s) {
                         if (s.hasMedia() && !s.paused) ss->acquire();
                         else ss->release();
                     });
    QObject::connect(controller.get(), &nuvio::mpv::MpvController::reachedEnd,
                     screensaver.get(),
                     &nuvio::integrations::ScreensaverInhibit::release);

    QObject::connect(playbackSession.get(),
                     &nuvio::playback::PlaybackSession::sessionChanged,
                     discord.get(), [dp = discord.get(), ps =
                                        playbackSession.get()] {
                         dp->setTitle(ps->currentTitle());
                     });


    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::quit,
                     &app, &QCoreApplication::quit);
    nuvio::ui::registerWith(engine);
    engine.rootContext()->setContextProperty(
        QStringLiteral("navigation"), QVariant::fromValue<QObject*>(
                                          navigation.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("appsettings"), QVariant::fromValue<QObject*>(
                                            settings.get()));
    auto prefApplier = std::make_unique<nuvio::ui::PreferencesApplier>(
        *settings, controller.get());
    prefApplier->applyAll();       // queued-safe even pre-init
    engine.rootContext()->setContextProperty(
        QStringLiteral("applier"),
        QVariant::fromValue<QObject*>(prefApplier.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("addons"), QVariant::fromValue<QObject*>(
                                       addonreg.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("streams"), QVariant::fromValue<QObject*>(
                                         streamResolver.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("playback"), QVariant::fromValue<QObject*>(
                                        playbackSession.get()));
    // Next-episode card rules + parental-guide lookup (P3a player behaviors).
    auto nextEpHelper =
        std::make_unique<nuvio::playback::NextEpisodeHelper>();
    engine.rootContext()->setContextProperty(
        QStringLiteral("nextep"), QVariant::fromValue<QObject*>(
                                      nextEpHelper.get()));
    auto parentalGuide =
        std::make_unique<nuvio::playback::ParentalGuideResolver>();
    engine.rootContext()->setContextProperty(
        QStringLiteral("parental"), QVariant::fromValue<QObject*>(
                                        parentalGuide.get()));
    // Skip-intro resolution (P3c): IntroDb base URL rides the env (Compose
    // bakes "" by default — blank disables the leg on both lines).
    auto skipResolver = std::make_unique<nuvio::playback::SkipResolver>();
    nuvio::playback::SkipResolver::Providers skipProviders;
    skipProviders.skipIntroEnabled = [sp = settings.get()] {
        return sp->skipIntroEnabled();
    };
    skipProviders.introDbBaseUrl = [] {
        return qEnvironmentVariable("NUVIO_INTRODB_URL");
    };
    skipProviders.introDbApiKey = [sp = settings.get()] {
        return sp->introDbApiKey();
    };
    skipProviders.introSubmitEnabled = [sp = settings.get()] {
        return sp->introSubmitEnabled();
    };
    skipResolver->setProviders(std::move(skipProviders));
    engine.rootContext()->setContextProperty(
        QStringLiteral("skip"), QVariant::fromValue<QObject*>(
                                    skipResolver.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("p2p"), QVariant::fromValue<QObject*>(
                                   p2pEngine.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("catalog"), QVariant::fromValue<QObject*>(
                                        catalog.get()));
    // Home rails (P4): addon-catalog shelves + settings, auto-refreshing
    // on registry changes (install/remove/enable, manifest arrivals).
    auto homeShelves = std::make_unique<nuvio::library::HomeShelves>(
        addonreg.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("homeshelves"), QVariant::fromValue<QObject*>(
                                           homeShelves.get()));
    homeShelves->refresh();
    // Recent searches (Compose search_history.properties parity); the QML
    // search page records/filters through this single instance.
    auto searchHistory =
        std::make_unique<nuvio::settings::SearchHistory>();
    engine.rootContext()->setContextProperty(
        QStringLiteral("searchHistory"),
        QVariant::fromValue<QObject*>(searchHistory.get()));
    // Hero ambient-trailer contexts (MetaPage backdrop autoplay).
    engine.rootContext()->setContextProperty(
        QStringLiteral("heroAmbientEnabled"), QVariant::fromValue(heroEnabled));
    if (heroController)
        engine.rootContext()->setContextProperty(
            QStringLiteral("heroController"),
            QVariant::fromValue<QObject*>(heroController.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("meta"), QVariant::fromValue<QObject*>(
                                    metaSvc.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("auth"), QVariant::fromValue<QObject*>(auth.get()));
    engine.addImageProvider(QStringLiteral("poster"),
                            new nuvio::ui::PosterProvider());  // engine takes ownership
    // Trailer resolution (systems/trailer): MetaPage ▶ Trailer clicks land
    // here; results open the player route via the shell's video page.
    auto trailerResolver =
        std::make_unique<nuvio::trailer::TrailerResolver>();
    // Tracking (T1): registry + scrobble pump. Providers register in T2+;
    // until then every dispatch is a silent no-op (nothing connected).
    auto trackingRegistry =
        std::make_unique<nuvio::tracking::TrackingRegistry>();
    auto scrobblePump = std::make_unique<nuvio::tracking::ScrobblePump>(
        trackingRegistry.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("tracking"), QVariant::fromValue<QObject*>(
                                        trackingRegistry.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("scrobble"), QVariant::fromValue<QObject*>(
                                        scrobblePump.get()));
    // Trakt (T2): device-code auth + scrobbler, registered into the
    // tracking registry (connected state follows auth). Client id/secret
    // ride NUVIO_TRAKT_CLIENT_ID/_SECRET (empty = inert, Compose parity).
    auto traktAuth = std::make_unique<nuvio::tracking::TraktAuth>();
    auto traktScrobbler = std::make_unique<nuvio::tracking::TraktScrobbler>(
        traktAuth.get(), trackingRegistry.get(),
        QString::fromLatin1(NUVIO_VERSION_STRING));
    engine.rootContext()->setContextProperty(
        QStringLiteral("trakt"), QVariant::fromValue<QObject*>(
                                     traktAuth.get()));
    // SIMKL (T3): PIN auth + direct scrobbler. Client id rides
    // NUVIO_SIMKL_CLIENT_ID (empty = inert, Compose parity).
    auto simklAuth = std::make_unique<nuvio::tracking::SimklAuth>(
        QString::fromLatin1(NUVIO_VERSION_STRING));
    auto simklScrobbler = std::make_unique<nuvio::tracking::SimklScrobbler>(
        simklAuth.get(), trackingRegistry.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("simkl"), QVariant::fromValue<QObject*>(
                                     simklAuth.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("trailer"), QVariant::fromValue<QObject*>(
                                       trailerResolver.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("mpvController"), QVariant::fromValue<QObject*>(
                                             controller.get()));
    // Watch-state foundation (systems/watching): Compose-parity resume +
    // watched persistence in the SHARED profile stores (watch_progress /
    // watched .properties, active profile key) so both builds read each
    // other.
    auto watchingStore =
        std::make_unique<nuvio::watching::WatchingStore>(
            nuvio::settings::ActiveProfile::id());
    auto watchRecorder = std::make_unique<nuvio::watching::WatchRecorder>(
        watchingStore.get());
    // P1b: the settings-blob CW payload applies into the recorder's store;
    // the orchestrator reloads it (emits cwPrefsChanged) on remote merges.
    syncOrch->setWatchRecorder(watchRecorder.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("watching"), QVariant::fromValue<QObject*>(
                                        watchRecorder.get()));

    // Progress-sync triggers: recorder commits -> debounced dirty push;
    // session (re)activation -> one full/delta pull then push. The
    // controller self-guards on signed-out state.
    QObject::connect(watchRecorder.get(),
                     &nuvio::watching::WatchRecorder::resumeChanged,
                     progressSync.get(),
                     &nuvio::authsync::ProgressSyncController::
                         onLocalProgressChanged);
    QObject::connect(watchRecorder.get(),
                     &nuvio::watching::WatchRecorder::continueWatchingChanged,
                     progressSync.get(),
                     &nuvio::authsync::ProgressSyncController::
                         onLocalProgressChanged);
    QObject::connect(watchRecorder.get(),
                     &nuvio::watching::WatchRecorder::watchedChanged,
                     progressSync.get(),
                     &nuvio::authsync::ProgressSyncController::
                         onWatchedChanged);
    // CW prefs ride the settings blob (P1b): local edits schedule a blob
    // push; remote merges reload through setWatchRecorder above.
    QObject::connect(watchRecorder.get(),
                     &nuvio::watching::WatchRecorder::cwPrefsChanged,
                     syncOrch.get(),
                     &nuvio::authsync::SyncOrchestrator::schedulePush);
    // Addons rows sync: registry changes -> debounced full-state push;
    // session activation -> server pull applied into the registry.
    auto addonsSync =
        std::make_unique<nuvio::authsync::AddonsSyncController>(
            addonreg.get(), nuvio::authsync::AuthConfig::load(),
            [ap = auth.get()] { return ap->accessToken(); });
    addonsSync->beginObserving();
    QObject::connect(
        auth.get(), &nuvio::authsync::AuthService::stateChanged,
        auth.get(),
        [as = addonsSync.get(), ap = auth.get()] {
            if (as && ap->sessionActive()) as->pullNow();
        });
    if (auth->sessionActive()) addonsSync->pullNow();
    // User library + collections (P5, Compose-shared profile stores).
    auto libraryStore = std::make_unique<nuvio::library::LibraryStore>(
        nuvio::settings::ActiveProfile::id());
    auto collectionStore =
        std::make_unique<nuvio::library::CollectionStore>(
            nuvio::settings::ActiveProfile::id());
    collectionStore->setAddonRegistry(addonreg.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("mylibrary"), QVariant::fromValue<QObject*>(
                                         libraryStore.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("collections"), QVariant::fromValue<QObject*>(
                                           collectionStore.get()));
    auto librarySync =
        std::make_unique<nuvio::authsync::LibrarySyncController>(
            nuvio::authsync::AuthConfig::load(),
            [ap = auth.get()] { return ap->accessToken(); },
            nuvio::settings::ActiveProfile::id());
    auto collectionSync =
        std::make_unique<nuvio::authsync::CollectionSyncController>(
            nuvio::authsync::AuthConfig::load(),
            [ap = auth.get()] { return ap->accessToken(); },
            nuvio::settings::ActiveProfile::id());
    QObject::connect(libraryStore.get(),
                     &nuvio::library::LibraryStore::changed,
                     librarySync.get(),
                     &nuvio::authsync::LibrarySyncController::
                         onLocalLibraryChanged);
    QObject::connect(collectionStore.get(),
                     &nuvio::library::CollectionStore::changed,
                     collectionSync.get(),
                     &nuvio::authsync::CollectionSyncController::
                         onLocalCollectionsChanged);
    QObject::connect(
        auth.get(), &nuvio::authsync::AuthService::stateChanged,
        auth.get(),
        [ls = librarySync.get(), cs = collectionSync.get(),
         ap = auth.get()] {
            if (ls && ap->sessionActive()) ls->fullLibrarySyncThenDeltas();
        });
    QObject::connect(
        auth.get(), &nuvio::authsync::AuthService::stateChanged,
        auth.get(),
        [cs = collectionSync.get(), ap = auth.get()] {
            if (cs && ap->sessionActive()) cs->pullNow();
        });
    // Home-catalog settings (Appendix A): standalone pull/push against
    // the shared platform (NOT the settings blob); local edits ride
    // prefsChanged like the fork's triggerPush call sites.
    auto homeCatalogSync =
        std::make_unique<nuvio::authsync::HomeCatalogSyncController>(
            nuvio::authsync::AuthConfig::load(),
            [ap = auth.get()] { return ap->accessToken(); },
            [ap = auth.get()] { return ap->userId(); },
            homeShelves.get(), nuvio::settings::ActiveProfile::id());
    QObject::connect(homeShelves.get(),
                     &nuvio::library::HomeShelves::prefsChanged,
                     homeCatalogSync.get(),
                     &nuvio::authsync::HomeCatalogSyncController::
                         onLocalCatalogChanged);
    QObject::connect(
        auth.get(), &nuvio::authsync::AuthService::stateChanged,
        auth.get(),
        [hs = homeCatalogSync.get(), ap = auth.get()] {
            if (hs && ap->sessionActive()) hs->pullNow();
        });
    if (auth->sessionActive()) {
        librarySync->fullLibrarySyncThenDeltas();
        collectionSync->pullNow();
        homeCatalogSync->pullNow();
    }
    // Episode-release alerts (A4): tracked shows reconcile against the
    // library above; refresh fires at startup like the fork's
    // LaunchedEffect (due releases notify while running; future ones
    // count as scheduled). Blob owns notifications_settings now.
    auto releaseNotifications =
        std::make_unique<nuvio::notifications::ReleaseNotificationManager>(
            libraryStore.get());
    syncOrch->setReleaseNotifications(releaseNotifications.get());
    QObject::connect(releaseNotifications.get(),
                     &nuvio::notifications::ReleaseNotificationManager::changed,
                     syncOrch.get(),
                     &nuvio::authsync::SyncOrchestrator::schedulePush);
    engine.rootContext()->setContextProperty(
        QStringLiteral("notifications"), QVariant::fromValue<QObject*>(
                                             releaseNotifications.get()));
    releaseNotifications->refreshAsync();
    // Supporter membership (A7): userId-scoped, NOT profile-scoped (the
    // cache keys on the account, like the fork) - so no fan-out target.
    // Auth changes drive verification; sign-out clears the cached access
    // (fork clearLocalState parity).
    auto memberAccess =
        std::make_unique<nuvio::membership::MemberAccess>(auth.get());
    auto membershipOverview =
        std::make_unique<nuvio::membership::MembershipOverview>(auth.get());
    auto community =
        std::make_unique<nuvio::membership::CommunityService>();
    QObject::connect(
        auth.get(), &nuvio::authsync::AuthService::stateChanged,
        auth.get(),
        [ma = memberAccess.get(), ap = auth.get()] {
            if (!ap->sessionActive()) ma->clearLocalState();
        });
    engine.rootContext()->setContextProperty(
        QStringLiteral("memberAccess"), QVariant::fromValue<QObject*>(
                                            memberAccess.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("membership"), QVariant::fromValue<QObject*>(
                                          membershipOverview.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("community"), QVariant::fromValue<QObject*>(
                                         community.get()));
    memberAccess->refresh();
    membershipOverview->refresh();
    // TMDB settings + id service (A5): the key travels the credential
    // family only; the blob carries the other 14 keys. Blob owns
    // tmdb_settings now (stripped of the key at assembly).
    auto tmdbSettings = std::make_unique<nuvio::tmdb::TmdbSettings>();
    auto tmdbService =
        std::make_unique<nuvio::tmdb::TmdbService>(tmdbSettings.get());
    syncOrch->setTmdbSettings(tmdbSettings.get());
    QObject::connect(tmdbSettings.get(), &nuvio::tmdb::TmdbSettings::changed,
                     syncOrch.get(),
                     &nuvio::authsync::SyncOrchestrator::schedulePush);
    engine.rootContext()->setContextProperty(
        QStringLiteral("tmdb"), QVariant::fromValue<QObject*>(
                                    tmdbSettings.get()));
    // MDBList ratings (A6): settings + service share the key; the blob
    // carries the other 9 keys, the key rides credentials only.
    auto mdbListSettings = std::make_unique<nuvio::mdblist::MdbListSettings>();
    auto mdbListService =
        std::make_unique<nuvio::mdblist::MdbListService>(
            mdbListSettings.get());
    syncOrch->setMdbListSettings(mdbListSettings.get());
    QObject::connect(mdbListSettings.get(),
                     &nuvio::mdblist::MdbListSettings::changed,
                     syncOrch.get(),
                     &nuvio::authsync::SyncOrchestrator::schedulePush);
    engine.rootContext()->setContextProperty(
        QStringLiteral("mdblist"), QVariant::fromValue<QObject*>(
                                       mdbListSettings.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("mdblistService"), QVariant::fromValue<QObject*>(
                                              mdbListService.get()));
    // Plugin scrapers (A8): full runtime (QuickJS + bridges). The
    // session consults enabled scrapers after addon-direct misses;
    // pull on session activation like the library leg.
    auto pluginRepo =
        std::make_unique<nuvio::plugins::PluginRepository>(
            auth.get(), tmdbService.get());
    playbackSession->setPluginExecutor(
        [pr = pluginRepo.get()](
            const QString& type, const QString& contentId, int season,
            int episode,
            nuvio::playback::PlaybackSession::PluginRowsCallback done) {
            pr->executeFor(
                type, contentId, season, episode,
                [done = std::move(done)](
                    const QList<nuvio::plugins::PluginStreamResult>& rows) {
                    QVariantList out;
                    for (const auto& r : rows) {
                        out.append(QVariantMap{
                            {QStringLiteral("url"), r.url},
                            {QStringLiteral("title"), r.title},
                            {QStringLiteral("source"),
                             r.provider.isEmpty()
                                 ? QStringLiteral("Plugin")
                                 : r.provider},
                        });
                    }
                    done(out);
                });
        });
    engine.rootContext()->setContextProperty(
        QStringLiteral("plugins"), QVariant::fromValue<QObject*>(
                                       pluginRepo.get()));
    QObject::connect(
        auth.get(), &nuvio::authsync::AuthService::stateChanged,
        auth.get(),
        [pr = pluginRepo.get(), ap = auth.get()] {
            if (pr && ap->sessionActive()) pr->pullFromServer();
        });
    if (auth->sessionActive()) pluginRepo->pullFromServer();
    pluginRepo->initialize();
    // Provider credentials (T4): the Qt-owned API keys through
    // the credential family (the settings blob strips them by policy).
    auto credsSync =
        std::make_unique<nuvio::authsync::ProviderCredsController>(
            settings.get(), tmdbSettings.get(), mdbListSettings.get(),
            nuvio::authsync::AuthConfig::load(),
            [ap = auth.get()] { return ap->accessToken(); },
            nuvio::settings::ActiveProfile::id());
    QObject::connect(settings.get(),
                     &nuvio::settings::AppSettings::playerOptionsChanged,
                     credsSync.get(),
                     &nuvio::authsync::ProviderCredsController::
                         onLocalCredsChanged);
    QObject::connect(tmdbSettings.get(), &nuvio::tmdb::TmdbSettings::changed,
                     credsSync.get(),
                     &nuvio::authsync::ProviderCredsController::
                         onLocalCredsChanged);
    QObject::connect(mdbListSettings.get(),
                     &nuvio::mdblist::MdbListSettings::changed,
                     credsSync.get(),
                     &nuvio::authsync::ProviderCredsController::
                         onLocalCredsChanged);
    QObject::connect(
        auth.get(), &nuvio::authsync::AuthService::stateChanged,
        auth.get(),
        [cs = credsSync.get(), ap = auth.get()] {
            if (cs && ap->sessionActive()) cs->syncNow();
        });
    if (auth->sessionActive()) credsSync->syncNow();
    // Debrid providers (D1): settings + auth live here; template engine,
    // resolver and cloud UI land in D2/D3. Context props for the D3 page.
    auto debridSettings = std::make_unique<nuvio::debrid::DebridSettings>();
    auto debridAuth =
        std::make_unique<nuvio::debrid::DebridAuth>(debridSettings.get());
    // Blob owns the debrid_settings feature now (stripped of credentials).
    syncOrch->setDebridSettings(debridSettings.get());
    QObject::connect(debridSettings.get(),
                     &nuvio::debrid::DebridSettings::changed,
                     syncOrch.get(),
                     &nuvio::authsync::SyncOrchestrator::schedulePush);    engine.rootContext()->setContextProperty(
        QStringLiteral("debrid"), QVariant::fromValue<QObject*>(
                                      debridSettings.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("debridauth"), QVariant::fromValue<QObject*>(
                                          debridAuth.get()));
    // Debrid unrestrict tier (D2): torrent entries resolve through the
    // configured provider before falling back to the P2P engine.
    auto debridResolver =
        std::make_unique<nuvio::debrid::DebridResolver>(debridSettings.get());
    playbackSession->setDebridResolver(debridResolver.get());
    // Offline downloads (A3): queue manager + offline-first playback.
    // A completed download plays from disk, skipping every network
    // tier (Compose MainAppContent + next-episode autoplay parity).
    auto downloadManager =
        std::make_unique<nuvio::downloads::DownloadManager>();
    playbackSession->setLocalFileProvider(
        [dm = downloadManager.get()](const QString& parent, int season,
                                     int episode, const QString& videoId) {
            return dm->playableLocalFile(parent, season, episode, videoId);
        });
    engine.rootContext()->setContextProperty(
        QStringLiteral("downloads"), QVariant::fromValue<QObject*>(
                                         downloadManager.get()));
    // Cloud library browser (D3): stored provider downloads.
    auto cloudLibrary =
        std::make_unique<nuvio::debrid::CloudLibrary>(debridSettings.get());
    engine.rootContext()->setContextProperty(
        QStringLiteral("cloud"), QVariant::fromValue<QObject*>(
                                     cloudLibrary.get()));
    {
        // One full/delta pull per session activation (initial + re-login).
        bool initialSyncFired = false;
        QPointer<nuvio::authsync::ProgressSyncController> ps(
            progressSync.get());
        QObject::connect(
            auth.get(), &nuvio::authsync::AuthService::stateChanged,
            auth.get(),
            [ps, ap = auth.get(), &initialSyncFired] {
                if (initialSyncFired || !ap->sessionActive()) return;
                initialSyncFired = true;
                if (ps) {
                    ps->fullSyncThenDeltas();
                    ps->fullWatchedSyncThenDeltas();
                }
            });
        if (auth->sessionActive()) {
            initialSyncFired = true;
            progressSync->fullSyncThenDeltas();
            progressSync->fullWatchedSyncThenDeltas();
        }
    }

    // User profiles (P7): local payload first (sets ActiveProfile), server
    // pull on session activation. Switching fans out through
    // activeProfileChanged: every profile-bound object reloads for the new
    // index (stores read per-access keys, so only cached state reloads).
    auto profileManager =
        std::make_unique<nuvio::profiles::ProfileManager>(
            nuvio::authsync::AuthConfig::load(),
            [ap = auth.get()] { return ap->accessToken(); });
    profileManager->setAuthUserId(auth->userId());
    profileManager->loadLocal();
    engine.rootContext()->setContextProperty(
        QStringLiteral("profiles"), QVariant::fromValue<QObject*>(
                                        profileManager.get()));
    QObject::connect(
        profileManager.get(),
        &nuvio::profiles::ProfileManager::activeProfileChanged,
        &app,
        [&](int index) {
            watchRecorder->setProfileId(index);
            libraryStore->setProfileId(index);
            collectionStore->setProfileId(index);
            addonreg->setProfileId(index);
            homeShelves->setProfileId(index);
            traktAuth->setProfileId(index);
            simklAuth->setProfileId(index);
            searchHistory->refresh();
            metaSvc->refreshSeasonViewMode();
            settings->refreshAll();
            syncOrch->setProfileId(index);
            progressSync->setProfileId(index);
            librarySync->setProfileId(index);
            collectionSync->setProfileId(index);
            homeCatalogSync->setProfileId(index);
            addonsSync->setProfileId(index);
            credsSync->setProfileId(index);
            debridAuth->setProfileId(index);
            downloadManager->setProfileId(index);
            releaseNotifications->setProfileId(index);
            tmdbSettings->setProfileId(index);
            mdbListSettings->setProfileId(index);
            pluginRepo->setProfileId(index);
        });
    QObject::connect(
        auth.get(), &nuvio::authsync::AuthService::stateChanged,
        auth.get(),
        [pm = profileManager.get(), ap = auth.get()] {
            pm->setAuthUserId(ap->userId());
            if (ap->sessionActive()) {
                pm->pullProfiles();
                pm->pullLocks();
            }
        });
    if (auth->sessionActive()) {
        profileManager->pullProfiles();
        profileManager->pullLocks();
    }

    const QUrl shellUrl(QStringLiteral("qrc:/nuvio/qml/MainShell.qml"));
    engine.load(shellUrl);
    if (engine.rootObjects().isEmpty()) {
        // Gate visibility rule: harness paths MUST explain themselves on
        // stderr regardless of any logging-category filtering.
        std::fprintf(stderr, "NUVIO-FATAL: QML shell failed to load: %s\n",
                     shellUrl.toString().toUtf8().constData());
        return 1;
    }

    qCInfo(lcNuvioAppStart).nospace()
        << "Nuvio Linux Qt " << NUVIO_VERSION_STRING
        << " (" << BUILD_TYPE_NAME << ", Qt" << QT_VERSION_STR
        << ", libmpv client-api "
        << (mpv_client_api_version() >> 16) << '.'
        << ((mpv_client_api_version() >> 8) & 0xff) << ")";

    // Route the CLI/smoke launch through the shell (harness parks playback
    // itself until the renderer announces its context).
    QObject* root = engine.rootObjects().first();
    if (!smoke && !launchUrl.isEmpty()) {
        QMetaObject::invokeMethod(root, "playFromLaunch",
                                  Q_ARG(QVariant, launchUrl));
    }

    // Player runtime persistence: volume level rides the shared profile
    // exactly like DesktopPlayerVolumeStorage (store nuvio_player_runtime,
    // key volume_level as a 0..1 float, 250 ms debounce on writes).
    {
        auto runtime =
            std::make_unique<nuvio::settings::PropertiesStore>(
                nuvio::settings::PropertiesStore::defaultPath(
                    "nuvio_player_runtime"));
        if (auto* item = root->findChild<nuvio::mpv::MpvQuickItem*>()) {
            if (const auto stored = runtime->getFloat("volume_level")) {
                const float level = std::clamp(*stored, 0.0f, 1.0f);
                item->setVolumePercent(level * 100.0);
            }
            // The debounce timer OWNS the store: it outlives this scope and
            // releases it exactly when the window goes away.
            auto* debounce = new QTimer(root);      // dies with the window
            debounce->setSingleShot(true);
            debounce->setInterval(250);
            QObject::connect(debounce, &QTimer::timeout, &app,
                             [rt = std::move(runtime), item] {
                                 if (item)
                                     rt->putFloat(
                                         "volume_level",
                                         static_cast<float>(
                                             item->volumePercent() / 100.0));
                             });
            QObject::connect(item,
                             &nuvio::mpv::MpvQuickItem::volumePercentChanged,
                             &app, [debounce] { debounce->start(); });
        }
    }


    // Route seeding: signed-out users land on Welcome; signed-in users
    // who never picked a profile land on selection; smoke harness and
    // settled sessions go straight to their working routes.
    if (!auth->sessionActive())
        navigation->replaceTop("welcome");
    else if (!smoke && !profileManager->hasEverSelectedProfile() &&
             profileManager->profilesVariant().size() > 1)
        navigation->replaceTop("profiles");

    if (smoke) {
        static nuvio::app::SmokeRunner runner;   // app-lifetime harness
        QMetaObject::invokeMethod(root, "setSmokeActive",
                                  Q_ARG(QVariant, true));
        runner.begin(root->findChild<nuvio::mpv::MpvQuickItem*>(),
                     controller.get(), smokeCfg);
    } else {
        QMetaObject::invokeMethod(root, "setSmokeActive",
                                  Q_ARG(QVariant, false));
    }

    const int rc = app.exec();

    // Ordered teardown BEFORE unique_ptrs unwind: window dies first so the
    // scenegraph invalidation frees the render context; only then does the
    // controller join its event thread and destroy the core.
    for (QObject* o : engine.rootObjects()) o->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    controller->shutdownAndWait();      // idempotent; joins unconditionally
    return rc;
}

