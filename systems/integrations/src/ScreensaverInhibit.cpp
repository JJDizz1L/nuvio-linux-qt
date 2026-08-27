#include "nuvio/integrations/ScreensaverInhibit.h"

#include <cstdio>

namespace nuvio::integrations {

QStringList ScreensaverInhibit::inhibitArgs()
{
    return {QStringLiteral("--who=nuvio-linux"),
            QStringLiteral("--what=sleep:idle"),
            QStringLiteral("--why=Nuvio playback"),
            QStringLiteral("sleep"),
            QStringLiteral("infinity")};
}

ScreensaverInhibit::ScreensaverInhibit(QObject* parent) : QObject(parent)
{
    connect(&m_proc, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError e) {
                if (e == QProcess::FailedToStart) {
                    std::fprintf(stderr,
                                 "screensaver: systemd-inhibit missing "
                                 "(no inhibition possible)\n");
                    m_active = false;
                }
            });
    // If the inhibitor dies on its own, forget it so a later acquire can
    // retry; a finished-but-unnoticed process would block all future holds.
    connect(&m_proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this] { m_active = false; });
}

ScreensaverInhibit::~ScreensaverInhibit()
{
    release();
}

void ScreensaverInhibit::acquire()
{
    if (m_active || m_proc.state() != QProcess::NotRunning) return;
    m_proc.start(QStringLiteral("systemd-inhibit"), inhibitArgs());
    // Async confirmation: active-on-faith, corrected by errorOccurred.
    m_active = true;
    std::fprintf(stderr, "screensaver: inhibited (systemd-inhibit)\n");
}

void ScreensaverInhibit::release()
{
    if (!m_active) return;
    m_active = false;
    m_proc.terminate();                    // SIGTERM drops the logind lock
    if (!m_proc.waitForFinished(1500)) m_proc.kill();
    std::fprintf(stderr, "screensaver: released\n");
}

} // namespace nuvio::integrations
