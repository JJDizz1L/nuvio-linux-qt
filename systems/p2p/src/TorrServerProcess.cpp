#include "nuvio/p2p/TorrServerProcess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace nuvio::p2p {

namespace {
constexpr int kPort              = 8091;
constexpr int kOrphanSettleMs    = 1000;
constexpr int kHealthIntervalMs  = 200;
constexpr int kStartupDeadlineMs = 15000;
constexpr int kStopGraceMs       = 3000;
} // namespace

QString resolveServerBinaryPath()
{
    // 1. explicit override wins (same contract as the Compose line).
    const QString configured = qEnvironmentVariable("NUVIO_TORRSERVER_BINARY");
    if (!configured.isEmpty()) return configured;

    // 2. dev-tree / vendor / cache candidates, CWD-relative in dev runs.
    const QString cacheHome = [] {
        const QString env = qEnvironmentVariable("XDG_CACHE_HOME");
        return env.isEmpty() ? QDir::homePath() + QStringLiteral("/.cache")
                             : env;
    }();
    // 2. CWD-independent app-dir candidates: packaged layout (binary next
    //    to the app) and the build-tree bundle (app/ -> ../native/...).
    QStringList candidates = [appDir = QCoreApplication::applicationDirPath()] {
        QStringList c;
        if (!appDir.isEmpty()) {
            c << QDir(appDir).filePath(QStringLiteral("TorrServer"))
              << QDir(appDir).filePath(QStringLiteral(
                     "../native/torrserver/linux-amd64/TorrServer"));
        }
        c << QStringLiteral(
                   "nuvio-linux-qt/build/native/torrserver/linux-amd64/TorrServer")
          << QStringLiteral(
                   "build/native/torrserver/linux-amd64/TorrServer")
          << QStringLiteral("vendor/TorrServer/dist/TorrServer-linux-amd64")
          << QStringLiteral("vendor/TorrServer/dist/TorrServer")
          // repo-checked-in GOAMD64=v1 binary (git-lfs): free dev-run candidate
          << QStringLiteral("composeApp/src/desktopMain/resources/torrserver/"
                            "linux-amd64/TorrServer");
        return c;
    }();
    candidates << cacheHome +
            QStringLiteral("/nuvio-linux/torrserver/bin/TorrServer");
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return c;
    return {};
}

TorrServerProcess::TorrServerProcess(QString configDirPath, QObject* parent)
    : QObject(parent), m_configDirPath(std::move(configDirPath)), m_health(new QNetworkAccessManager(this))
{
    m_orphanTimer.setSingleShot(true);
    connect(&m_orphanTimer, &QTimer::timeout, this, [this] { spawn(m_pendingBinaryPath); });
    m_pollTimer.setInterval(kHealthIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &TorrServerProcess::pollHealth);
    m_deadlineTimer.setSingleShot(true);
    connect(&m_deadlineTimer, &QTimer::timeout, this, [this] {
        if (m_phase == Phase::Starting) {
            finishFailure(QStringLiteral("TorrServer failed to start within 15s"));
            stop();
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError err) {
                if (err == QProcess::FailedToStart && m_phase == Phase::Starting)
                    finishFailure(QStringLiteral("cannot start TorrServer binary (missing or not executable)"));
            });
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode) {
                if (m_phase == Phase::Starting)
                    finishFailure(QStringLiteral("TorrServer process died on startup (exit code %1)").arg(exitCode));
            });
}

TorrServerProcess::~TorrServerProcess()
{
    stop();
}

QString TorrServerProcess::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(kPort);
}

bool TorrServerProcess::isRunning() const
{
    return m_phase == Phase::Running;
}

void TorrServerProcess::start(const QString& binaryPath)
{
    if (m_phase == Phase::Running) {
        emit ready();
        return;
    }
    if (m_phase != Phase::Idle) return;   // a start is already in flight

    if (binaryPath.isEmpty()) {
        // Deferred ON PURPOSE: callers install signal connections before
        // calling start(); synchronous emission here would race their
        // token bookkeeping.
        QMetaObject::invokeMethod(this, [this] {
            finishFailure(QStringLiteral(
                "TorrServer binary not found; set NUVIO_TORRSERVER_BINARY"));
        }, Qt::QueuedConnection);
        return;
    }
    m_pendingBinaryPath = binaryPath;
    enterOrphanWaitAndSpawn(binaryPath);
}

void TorrServerProcess::enterOrphanWaitAndSpawn(const QString& binaryPath)
{
    Q_UNUSED(binaryPath);   // path rides m_pendingBinaryPath across the settle gap
    m_phase = Phase::OrphanWait;
    // A leftover instance on our port would make the fresh server fail to
    // bind. Ask it to shut down (fire-and-forget) and give it a moment.
    QNetworkReply* rep = m_health->get(QNetworkRequest(QUrl(baseUrl() + QStringLiteral("/shutdown"))));
    connect(rep, &QNetworkReply::finished, rep, &QObject::deleteLater);
    m_orphanTimer.start(kOrphanSettleMs);   // -> spawn(m_pendingBinaryPath)
}

void TorrServerProcess::spawn(const QString& binaryPath)
{
    QDir().mkpath(m_configDirPath);
    QFile binary(binaryPath);
    if (!binary.exists()) {
        finishFailure(QStringLiteral("TorrServer binary not found at %1").arg(binaryPath));
        return;
    }
    const QFile::Permissions exe = QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther;
    if ((binary.permissions() & exe) != exe) binary.setPermissions(binary.permissions() | exe);

    m_process.setProgram(binaryPath);
    m_process.setArguments({QStringLiteral("--port"), QString::number(kPort),
                            QStringLiteral("--ip"), QStringLiteral("127.0.0.1"),
                            QStringLiteral("--path"), m_configDirPath});
    m_process.setWorkingDirectory(m_configDirPath);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start();
    if (m_process.state() == QProcess::NotRunning) {
        finishFailure(QStringLiteral("cannot start TorrServer binary"));
        return;
    }
    m_phase = Phase::Starting;
    m_deadlineTimer.start(kStartupDeadlineMs);
    m_pollTimer.start();
}

void TorrServerProcess::pollHealth()
{
    if (m_phase != Phase::Starting) return;
    QNetworkRequest req{QUrl(baseUrl() + QStringLiteral("/echo"))};
    req.setTransferTimeout(5000);
    QNetworkReply* rep = m_health->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_phase == Phase::Starting && status >= 200 && status < 300) {
            m_phase = Phase::Running;
            m_pollTimer.stop();
            m_deadlineTimer.stop();
            emit ready();
        }
        // Non-2xx or network errors just mean "not up yet": the poll and
        // the deadline timer own retry/failure semantics.
    });
}

void TorrServerProcess::stop()
{
    m_orphanTimer.stop();
    m_pollTimer.stop();
    m_deadlineTimer.stop();
    if (m_phase == Phase::Idle) { m_pendingBinaryPath.clear(); return; }
    m_phase = Phase::Idle;
    // Graceful first: /shutdown endpoint, then SIGTERM, then SIGKILL.
    QNetworkReply* rep = m_health->get(QNetworkRequest(QUrl(baseUrl() + QStringLiteral("/shutdown"))));
    connect(rep, &QNetworkReply::finished, rep, &QObject::deleteLater);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        QTimer::singleShot(kStopGraceMs, this, [this] {
            if (m_process.state() != QProcess::NotRunning) m_process.kill();
        });
    }
    m_pendingBinaryPath.clear();
}

void TorrServerProcess::finishFailure(QString reason)
{
    stop();   // resets phase + timers (still emits nothing)
    emit failed(reason);
}

} // namespace nuvio::p2p
