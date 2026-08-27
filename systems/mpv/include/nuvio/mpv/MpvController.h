// Owner of the mpv core lifecycle. ALL non-render API access happens on ONE
// dedicated thread (drain-command-queue then wait_event loop) — the invariant
// the Compose bridge fought races over, made physical by construction here
// (nothing else holds or can reach the handle; the renderer gets a guarded
// one-shot accessor for its context creation only).
//
// Principle compliance: the loop polls events with a fixed slice purely for
// core liveness/shutdown responsiveness. It schedules NOTHING time-shaped in
// playback: frame selection is mpv's engine untouched (stock video-sync),
// presentation belongs to the scene-graph consumer. See plan timing directive.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#include "nuvio/mpv/MpvTypes.h"

struct mpv_handle;   // fwd: C type declared globally in <mpv/client.h>
struct mpv_event;

namespace nuvio::mpv {

class MpvController final : public QObject {
    Q_OBJECT
public:
    explicit MpvController(QObject* parent = nullptr);
    ~MpvController() override;              // joins everything; never leaks

    /** Asynchronous init; emits ready(ok,error). Command-safe beforehand. */
    void start();

    /** Idempotent; joins the event thread before returning. */
    void shutdownAndWait();

    [[nodiscard]] bool isReady() const { return m_ready.load(std::memory_order_acquire); }

    // ---- Any-thread command surface (queued onto the event thread) --------
    Q_INVOKABLE void enqueueCommand(const QStringList& args);
    void loadFile(const QString& url, const QString& audioFile = {},
                  qint64 startMillis = -1);
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE void seekToSeconds(double seconds, bool absolute = true);
    Q_INVOKABLE void setVolumePercent(double percent);
    Q_INVOKABLE void setSpeedFactor(double factor);
    Q_INVOKABLE void cycleMute();
    void        setPropertyString(const QString& name, const QString& value);

    [[nodiscard]] PlaybackSnapshot snapshot() const;   ///< locked copy

    /**
     * Renderer-thread one-shot: returns the raw handle during
     * mpv_render_context_create only. Never stored elsewhere.
     */
    mpv_handle* handleForRenderInit();

    /** Queued refinement once the GL_VENDOR string becomes known. */
    Q_INVOKABLE void applyGlVendorLower(const QString& vendorLower);

    /** Diagnostics mirror of the renderer's published-frame counter, if wired. */
    void attachRenderStats(std::weak_ptr<class RenderStats> s);

    /** DEBUG (any-thread): last event-thread-loop core readings. */
    [[nodiscard]] QString debugCoreState();

    // Event-thread-recorded ground truth (atomics: readers are other threads).
    std::atomic<int>    m_dbgTpRc{-2};      ///< mpv_get_property("time-pos") rc
    std::atomic<int>    m_dbgDurRc{-2};     ///< mpv_get_property("duration") rc
    std::atomic<int>    m_dbgPauseFlag{-1}; ///< observed pause flag (-1 unset)
    std::atomic<int>    m_dbgIdleActive{-1};
    std::atomic<int>    m_dbgPubs{0};       ///< successful snapshotChanged emits
    std::atomic<long long> m_dbgTpValX100{-99999}; ///< last raw time-pos*100

signals:
    void ready(bool ok, QString error);
    void snapshotChanged(nuvio::mpv::PlaybackSnapshot snapshot);
    void trackListChanged(QVector<nuvio::mpv::TrackInfo> tracks);
    void decoderChanged(QString hwdecCurrent);  ///< VALUE-CHANGE-ONLY logging
    void libmpvMessage(int level, QString text);
    void fileLoaded();
    void reachedEnd();
private:
    void threadMain();
    bool initializeCoreOnThread(QString* errOut);
    void processEvent(struct mpv_event* ev);
    /**
     * Rate-limited (~15 Hz) broadcast for high-rate fields. urgent=true
     * (pause/eof/paused-for-cache flips) bypasses the interval: coalescing
     * a flag flip is how the item ends up permanently stale — pausing
     * silences the very event stream that would otherwise flush it.
     */
    void publishSnapshotPlayback(bool urgent = false);
    void maybeAutoFallbackDecode(const QByteArray& msgText, int level);
    void executeOnThread(const QStringList& args);
    [[nodiscard]] static QString copyModeOf(const QString& chain);

    mpv_handle*    m_handle = nullptr;

    std::thread          m_thread;
    std::atomic<bool>    m_ready{false};
    std::atomic<bool>    m_stop{false};
    std::atomic<bool>    m_joined{false};
    std::atomic<bool>    m_vendorApplied{false};
    std::atomic<bool>    m_noAutoFallback{false};

    mutable std::mutex       m_cmdMutex;
    std::deque<QStringList>  m_cmdQueue;

    mutable std::mutex   m_snapMutex;
    PlaybackSnapshot     m_snapshot;

    // Event-thread-only state below (no locking):
    QString              m_prevHwdec;        // for change-only logging
    QString              m_baseChain;
    QString              m_activeChain;
    int                  m_failStreak      = 0;
    qint64               m_streakStartMs   = 0;
    bool                 m_fallbackSticky  = false;
    qint64               m_lastPublishMs   = 0;
    std::weak_ptr<class RenderStats> m_renderStats; // diagnostics only
};

} // namespace nuvio::mpv