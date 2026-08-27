#pragma once

// Screensaver inhibit during playback (plan P3; Compose parity of the
// chain in AGENTS.md "Screensaver inhibit & KDE sleep").
//
// Slice-1 scope: the systemd path - `systemd-inhibit --who=nuvio-linux
// --what=sleep:idle --why=... sleep infinity` held for exactly as long as
// playback is active. This is the native logind lock and the primary path
// on every systemd desktop (Arch/omarchy included). The portal fallback
// (flatpak sandboxes) and KDE PolicyAgent specifics come with packaging.
//
// Lifecycle: acquire() when media plays, release() on pause/end/teardown;
// both idempotent. The child is deliberately `sleep infinity` - killing it
// drops the lock atomically (no D-Bus cookie bookkeeping to lose).

#include <QObject>
#include <QProcess>

namespace nuvio::integrations {

class ScreensaverInhibit final : public QObject {
    Q_OBJECT
public:
    explicit ScreensaverInhibit(QObject* parent = nullptr);
    ~ScreensaverInhibit() override;

    /// Builds the exact argv (pure; offline-testable).
    [[nodiscard]] static QStringList inhibitArgs();

public slots:
    void acquire();     ///< idempotent
    void release();     ///< idempotent

private:
    QProcess m_proc;
    bool     m_active = false;
};

} // namespace nuvio::integrations