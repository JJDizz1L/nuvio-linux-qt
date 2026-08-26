#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <clocale>
#include <cstdio>
#include <memory>

#include <mpv/client.h>

#include "bootstrap/LogCategories.h"
#include "bootstrap/ModuleRegistry.h"
#include "bootstrap/SmokeRunner.h"
#include "Version.h"                    // generated via configure_file
#include "nuvio/mpv/MpvController.h"
#include "nuvio/mpv/MpvLog.h"
#include "nuvio/mpv/MpvQuickItem.h"
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

    QGuiApplication::setApplicationName(QStringLiteral("nuvio-linux"));
    QGuiApplication::setApplicationVersion(QStringLiteral(NUVIO_VERSION_STRING));
    QGuiApplication::setOrganizationDomain(QStringLiteral("io.github.jdizz1l"));
    // Stable compositor identity (plan W1): one app_id for dev AND packaged
    // runs; the desktop file may be absent locally — harmless no-op then.
    QGuiApplication::setDesktopFileName(
        QStringLiteral("io.github.jjdizz1l.NuvioLinux"));

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

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::quit,
                     &app, &QCoreApplication::quit);
    nuvio::ui::registerWith(engine);
    engine.rootContext()->setContextProperty(
        QStringLiteral("mpvController"), QVariant::fromValue<QObject*>(
                                             controller.get()));

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

