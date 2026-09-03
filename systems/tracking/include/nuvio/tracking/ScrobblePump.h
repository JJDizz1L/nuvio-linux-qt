#pragma once

// Scrobble pump (T1): translates player tick state into coordinator calls,
// mirroring Compose PlayerScreenRuntime's scrobble edges (start once per
// item, pause on rising edge, stop once at >=80% completion, seek jumps
// stop + restart on STOP_AND_RESTART providers). Fire-and-forget; provider
// failures are logged by the caller, never raised.
//
// Seek detection is positional (no player signals exist for it here): a
// >5 s discontinuity between 1 Hz ticks counts as a seek. Normal playback
// advances ~1 s per tick, so the threshold never false-fires on steady
// play (stalls freeze the position instead of jumping it).

#include <QObject>
#include <QString>

#include "nuvio/tracking/TrackingRegistry.h"

namespace nuvio::tracking {

class ScrobblePump final : public QObject {
    Q_OBJECT

public:
    explicit ScrobblePump(TrackingRegistry* registry,
                          QObject* parent = nullptr);

    /// New playback intent (session change). Resets per-item latches.
    Q_INVOKABLE void beginItem(const QString& type, const QString& id,
                               const QString& title);
    /// 1 Hz player tick (VideoPage pump). No-ops without a begun item.
    Q_INVOKABLE void tick(qint64 positionMs, qint64 durationMs, bool paused);

signals:
    /// Diagnostic surface (tests + future logging): what was dispatched.
    void dispatched(const QString& action, double progressPercent);

private:
    void dispatch(ScrobbleAction action, double progress);
    void dispatchSeekStop(double progress);

    TrackingRegistry* m_registry = nullptr;
    TrackingMedia m_media;
    bool m_hasItem = false;
    bool m_startSent = false;
    bool m_completionSent = false;
    bool m_wasPaused = false;
    qint64 m_lastPosMs = -1;
};

} // namespace nuvio::tracking
