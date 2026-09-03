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
#include "nuvio/authsync/ProgressSyncController.h"
#include "nuvio/authsync/AuthService.h"
#include "nuvio/authsync/SyncOrchestrator.h"
#include "nuvio/library/AddonRegistry.h"
#include "nuvio/settings/PropertiesStore.h"
#include "nuvio/settings/SearchHistory.h"
#include "nuvio/settings/SyncIdentity.h"
#include "nuvio/library/CatalogService.h"
#include "nuvio/library/MetaService.h"
#include "nuvio/playback/PlaybackSession.h"
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
            [ap = auth.get()] { return ap->accessToken(); }, 1);
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
    engine.rootContext()->setContextProperty(
        QStringLiteral("p2p"), QVariant::fromValue<QObject*>(
                                   p2pEngine.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("catalog"), QVariant::fromValue<QObject*>(
                                        catalog.get()));
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
    engine.rootContext()->setContextProperty(
        QStringLiteral("trailer"), QVariant::fromValue<QObject*>(
                                       trailerResolver.get()));
    engine.rootContext()->setContextProperty(
        QStringLiteral("mpvController"), QVariant::fromValue<QObject*>(
                                             controller.get()));
    // Watch-state foundation (systems/watching): Compose-parity resume +
    // watched persistence in the SHARED profile stores (watch_progress /
    // watched .properties, profile key 1) so both builds read each other.
    auto watchingStore =
        std::make_unique<nuvio::watching::WatchingStore>(
            nuvio::watching::kDefaultProfileId);
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


    // Route seeding: signed-out users land on Welcome; smoke harness and
    // signed-in sessions go straight to their working routes.
    if (!auth->sessionActive()) navigation->replaceTop("welcome");

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

