#pragma once

// TorrServer process lifecycle (parity port of TorrServerBinary.kt):
// spawns the server bound to 127.0.0.1:8091 with its config/cache directory,
// probes /echo until the HTTP surface answers (15 s deadline), shuts down a
// leftover instance on a busy port BEFORE spawning, and stops gracefully
// (/shutdown + SIGTERM, SIGKILL after a 3 s grace). Fully event-loop driven;
// never blocks the UI thread.
//
// Failure surfaces exclusively through failed(reason) - including the
// pre-flight "binary path could not be resolved" case - so orchestrators
// need no null-state checks. Failures are emitted QUEUED, never
// synchronously from within start(), which keeps callers' token bookkeeping
// race-free.

#include <QProcess>
#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;

namespace nuvio::p2p {

/// Candidate search honored when launching: $NUVIO_TORRSERVER_BINARY wins,
/// then dev-tree/vendor/cache-relative locations (Compose-line candidate
/// list adapted to this repo's layout). Empty result = not found.
[[nodiscard]] QString resolveServerBinaryPath();

class TorrServerProcess final : public QObject {
    Q_OBJECT
public:
    explicit TorrServerProcess(QString configDirPath,
                               QObject* parent = nullptr);
    ~TorrServerProcess() override;

    [[nodiscard]] QString baseUrl() const;
    [[nodiscard]] bool   isRunning() const;

public slots:
    /// Async. Empty path fails immediately (queued signal).
    void start(const QString& binaryPath);
    /// Graceful stop; safe from any phase, idempotent.
    void stop();

signals:
    void ready();
    void failed(const QString& reason);

private:
    enum class Phase { Idle, OrphanWait, Starting, Running };

    void enterOrphanWaitAndSpawn(const QString& binaryPath);
    void spawn(const QString& binaryPath);
    void pollHealth();
    void finishFailure(QString reason);

    Phase     m_phase = Phase::Idle;
    const QString m_configDirPath;
    QString   m_pendingBinaryPath;  ///< carried across the orphan-settle gap
    QProcess  m_process;
    QTimer    m_orphanTimer;   ///< 1 s settle after orphan shutdown
    QTimer    m_pollTimer;     ///< 200 ms /echo ticks
    QTimer    m_deadlineTimer; ///< 15 s startup deadline
    QNetworkAccessManager* m_health = nullptr;
};

} // namespace nuvio::p2p