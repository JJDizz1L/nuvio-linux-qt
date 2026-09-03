// The one QML type of the video system. Binds controller signals onto
// properties, forwards keys/wheel during media playback (keyboard ownership
// directive), and provides the QQuickFramebufferObject plumbing whose
// renderer lives on the scene-graph thread (see MpvRenderer.h).
#pragma once

#include <QPointer>
#include <QQuickFramebufferObject>

#include <memory>

#include "nuvio/mpv/MpvController.h"
#include "nuvio/mpv/MpvTypes.h"

namespace nuvio::mpv {

class MpvRenderer;
struct RenderStats;

class MpvQuickItem : public QQuickFramebufferObject {
    // NOTE: intentionally NOT final — Qt derives QQmlElement<T> for every
    // QML-registered type; deriving is framework-owned, not our choice.
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ controllerObj WRITE setControllerObj NOTIFY controllerChanged)
    Q_PROPERTY(bool      hasMedia   READ hasMedia     NOTIFY hasMediaChanged)
    Q_PROPERTY(qint64    positionMs READ positionMs   NOTIFY positionMsChanged)
    Q_PROPERTY(qint64    durationMs READ durationMs   NOTIFY durationMsChanged)
    Q_PROPERTY(bool      paused     READ paused       NOTIFY pausedChanged)
    Q_PROPERTY(bool      buffering  READ buffering    NOTIFY bufferingChanged)
    Q_PROPERTY(int       volumePercent READ volumePercent WRITE setVolumePercent NOTIFY volumePercentChanged)
    Q_PROPERTY(QString   hwdecCurrent  READ hwdecCurrent  NOTIFY hwdecCurrentChanged)
    Q_PROPERTY(double    cacheSeconds  READ cacheSeconds  NOTIFY cacheSecondsChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)

public:
    explicit MpvQuickItem(QQuickItem* parent = nullptr);

    // QQuickFramebufferObject
    Renderer* createRenderer() const override;

    [[nodiscard]] MpvController* controller() const { return m_controller.data(); }
    QObject*                     controllerObj() const;   // impl in cpp

    [[nodiscard]] bool    hasMedia()      const;
    [[nodiscard]] qint64  positionMs()    const;
    [[nodiscard]] qint64  durationMs()    const;
    [[nodiscard]] bool    paused()        const;
    [[nodiscard]] bool    buffering()     const;
    [[nodiscard]] int     volumePercent() const;
    [[nodiscard]] QString hwdecCurrent()  const;
    [[nodiscard]] double  cacheSeconds()  const;
    [[nodiscard]] QVariantList tracks() const { return m_tracks; }

    // Called by MpvRenderer (scene-graph thread).
    void reportGlVendorToController(const QString& vendorLower);
    void publishRenderStatsIfFirst(const std::weak_ptr<RenderStats>& s);

    // ---- QML / harness API -------------------------------------------------
    Q_INVOKABLE [[nodiscard]] PlaybackSnapshot snapshotPublic() const
    { return m_cachedSnap; }
    [[nodiscard]] std::shared_ptr<RenderStats> renderStats() const
    { return m_renderStats.lock(); }

signals:
    /** Fired once, after mpv_render_context_create succeeded. */
    void renderContextReady();

private:
    // Playback-request buffering: mpv cannot open its VO before the render
    // context exists, so early play() calls are parked here and flushed by
    // notifyRenderContextReady() (embedding-order race, offscreen/CI safe).
    std::atomic<bool>  m_ctxReady{false};
    QString            m_pendingUrl;
    QString            m_pendingAudio;
    qint64             m_pendingStartMs = -1;

public slots:
    void requestUpdate();               ///< invoked queued by update callback
    void setControllerObj(QObject* obj);
    void setVolumePercent(int percent);
    void applySnapshot(nuvio::mpv::PlaybackSnapshot snap);
    /** Scene-graph thread → UI thread announcement (render ctx is live). */
    void notifyRenderContextReady();

    // ---- QML API ----------------------------------------------------------
    Q_INVOKABLE void play(const QString& url, const QString& audioUrl = {},
                          qint64 startMs = -1);
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void seekBySeconds(double deltaSec);
    Q_INVOKABLE void seekToSeconds(double absoluteSec);
    Q_INVOKABLE void stop();
    Q_INVOKABLE QString formatTime(double seconds) const;

    /// Plan directive W2: forward a media key to mpv verbatim by name
    /// ("Space", "Right", "F7", ...). mpv resolves it against default
    /// bindings + the user's input.conf; keys nothing claims are harmless
    /// no-ops there (QML-side app shortcuts stay independent).
    Q_INVOKABLE bool sendKey(const QString& mpvKeyName);

    /// Track selection: `kind` is "audio"|"sub", id <= 0 disables the track
    /// (mpv `set aid/sid no`). Issues explicit selection, never alang/slang.
    Q_INVOKABLE void setTrack(const QString& kind, int id);

    /// Transient playback speed for hold-to-speed (mpv `set speed`; range
    /// mirrors mpv's 0.01..100). Not persisted - release restores 1.0.
    Q_INVOKABLE void setSpeed(double speed);

signals:
    void controllerChanged();
    void hasMediaChanged();
    void positionMsChanged();
    void durationMsChanged();
    void pausedChanged();
    void bufferingChanged();
    void volumePercentChanged();
    void hwdecCurrentChanged();
    void cacheSecondsChanged();
    void tracksChanged();
    void doubleClicked();

protected:
    void keyPressEvent(QKeyEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void componentComplete() override;

private:
    void ensureConnections();
    void forwardKey(const QString& commandEnvelopeName, const QString& keyText);

    QPointer<MpvController> m_controller;
    PlaybackSnapshot        m_cachedSnap;
    QVariantList            m_tracks;
    double                  m_volume = 100.0;
    bool                    m_connectionsDone = false;
    std::atomic<bool>       m_statsAttached{false};
    std::weak_ptr<RenderStats> m_renderStats;
};

} // namespace nuvio::mpv