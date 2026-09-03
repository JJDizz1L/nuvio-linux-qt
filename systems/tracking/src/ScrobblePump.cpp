#include "nuvio/tracking/ScrobblePump.h"

#include <algorithm>

#include <QLoggingCategory>

#include "nuvio/settings/ActiveProfile.h"

namespace nuvio::tracking {

namespace {
constexpr qint64 kSeekJumpThresholdMs = 5000;
constexpr double kCompletionPercent = 80.0;

Q_LOGGING_CATEGORY(lcScrobble, "nuvio.tracking.scrobble", QtWarningMsg)
} // namespace

ScrobblePump::ScrobblePump(TrackingRegistry* registry, QObject* parent)
    : QObject(parent), m_registry(registry)
{}

void ScrobblePump::beginItem(const QString& type, const QString& id,
                             const QString& title)
{
    m_media = mediaForPlayback(type, id, title);
    m_hasItem = true;
    m_startSent = false;
    m_completionSent = false;
    m_wasPaused = false;
    m_lastPosMs = -1;
}

void ScrobblePump::tick(qint64 positionMs, qint64 durationMs, bool paused)
{
    if (!m_hasItem || !m_registry) return;
    const double progress =
        durationMs > 0
            ? std::clamp(100.0 * double(positionMs) / double(durationMs),
                         0.0, 100.0)
            : 0.0;

    // Seek jump: stop (restart-policy providers) + re-arm start.
    if (m_lastPosMs >= 0 && positionMs >= 0 &&
        qAbs(positionMs - m_lastPosMs) > kSeekJumpThresholdMs) {
        dispatchSeekStop(progress);
        m_startSent = false;
        m_completionSent = false;
    }
    if (positionMs >= 0) m_lastPosMs = positionMs;

    if (!m_startSent && positionMs >= 0) {
        m_startSent = true;
        dispatch(ScrobbleAction::Start, progress);
    }
    if (paused && !m_wasPaused)
        dispatch(ScrobbleAction::Pause, progress);
    m_wasPaused = paused;
    if (!m_completionSent && progress >= kCompletionPercent) {
        m_completionSent = true;
        dispatch(ScrobbleAction::Stop, progress);
    }
}

void ScrobblePump::dispatch(ScrobbleAction action, double progress)
{
    ScrobbleEvent event;
    event.media = m_media;
    event.progressPercent = progress;
    const auto failures = ScrobbleCoordinator::scrobble(
        *m_registry, nuvio::settings::ActiveProfile::id(),
        nuvio::settings::ActiveProfile::id(), action, event);
    for (const auto& f : failures)
        qCWarning(lcScrobble) << "scrobble" << scrobbleWireValue(action)
                              << "failed for"
                              << providerStorageId(f.provider) << f.message;
    emit dispatched(scrobbleWireValue(action), progress);
}

void ScrobblePump::dispatchSeekStop(double progress)
{
    ScrobbleEvent event;
    event.media = m_media;
    event.progressPercent = progress;
    const auto failures = ScrobbleCoordinator::scrobbleSeek(
        *m_registry, nuvio::settings::ActiveProfile::id(),
        nuvio::settings::ActiveProfile::id(), ScrobbleAction::Stop, event);
    for (const auto& f : failures)
        qCWarning(lcScrobble) << "seek scrobble failed for"
                              << providerStorageId(f.provider) << f.message;
    emit dispatched(QStringLiteral("seek-stop"), progress);
}

} // namespace nuvio::tracking
