#include "nuvio/mpv/MpvQuickItem.h"

#include "nuvio/mpv/MpvController.h"
#include "nuvio/mpv/MpvKeyMap.h"
#include "nuvio/mpv/MpvLog.h"
#include "nuvio/mpv/MpvRenderer.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace nuvio::mpv {

namespace {
QString formatTimeStatic(double seconds)
{
    if (!(seconds >= 0)) seconds = 0;           // also filters NaN
    const qint64 total = qint64(seconds + 0.5);
    const qint64 h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3")
                       .arg(h).arg(m, 2, 10, u'0').arg(s, 2, 10, u'0')
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, u'0');
}
} // namespace

MpvQuickItem::MpvQuickItem(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
{
    setMirrorVertically(false);     // FLIP_Y=1 in the renderer handles orient.
    setAcceptedMouseButtons(Qt::LeftButton);
}

QQuickFramebufferObject::Renderer* MpvQuickItem::createRenderer() const
{
    // The renderer wraps this item by reference for its whole lifetime;
    // createRenderer() is invoked during scene-graph sync where mutation of
    // non-item state is the renderer's own domain. Standard QQuickFBO idiom.
    return new MpvRenderer(const_cast<MpvQuickItem&>(*this));
}

bool   MpvQuickItem::hasMedia() const { return m_cachedSnap.hasMedia(); }

QObject* MpvQuickItem::controllerObj() const
{
    // Full-type conversion lives here; the header only forward-declares.
    return static_cast<QObject*>(m_controller.data());
}
qint64 MpvQuickItem::positionMs() const
{ return m_cachedSnap.positionSec < 0 ? -1 : qint64(m_cachedSnap.positionSec * 1000.0); }
qint64 MpvQuickItem::durationMs() const { return qint64(m_cachedSnap.durationSec * 1000.0); }
bool   MpvQuickItem::paused() const { return m_cachedSnap.paused; }
bool   MpvQuickItem::buffering() const
{ return m_cachedSnap.pausedForCache && hasMedia(); }
int     MpvQuickItem::volumePercent() const { return int(qRound(m_volume)); }
QString MpvQuickItem::hwdecCurrent() const { return m_cachedSnap.hwdecCurrent; }

double MpvQuickItem::cacheSeconds() const
{
    return m_cachedSnap.demuxerCacheSec;
}

void MpvQuickItem::setControllerObj(QObject* obj)
{
    auto* c = qobject_cast<MpvController*>(obj);
    std::fprintf(stderr, "[nuvio.qt] setControllerObj obj=%p resolved=%p this=%p\n",
                 static_cast<void*>(obj), static_cast<void*>(c),
                 static_cast<void*>(this));
    if (c == m_controller.data()) return;
    m_controller = c;
    ensureConnections();
    emit controllerChanged();
}

void MpvQuickItem::ensureConnections()
{
    // Connect EAGERLY once: mpv signals cannot fire before the worker
    // finishes init, so there is zero benefit to a ready-gated deferred
    // connect — and that deferred variant raced (snapshot pipe stayed dead).
    MpvController* c = m_controller.data();
    if (!c || m_connectionsDone) return;
    m_connectionsDone = true;

    const bool okA = connect(c, &MpvController::snapshotChanged,
            this, &MpvQuickItem::applySnapshot, Qt::QueuedConnection);
    const bool okB = connect(c, &MpvController::reachedEnd, this, [this, c] {
        PlaybackSnapshot s = c->snapshot();
        s.eofReached = true;
        applySnapshot(std::move(s));
    }, Qt::QueuedConnection);
    const bool okT = connect(c, &MpvController::trackListChanged, this,
            [this](const QVector<TrackInfo>& tracks) {
                QVariantList out;
                out.reserve(tracks.size());
                for (const TrackInfo& t : tracks) {
                    QVariantMap m;
                    const char* kindName =
                        t.kind == TrackKind::Audio     ? "audio"
                        : t.kind == TrackKind::Video    ? "video"
                        : t.kind == TrackKind::Subtitle ? "sub"
                                                        : "other";
                    m.insert(QStringLiteral("kind"),
                             QString::fromLatin1(kindName));
                    m.insert(QStringLiteral("id"),   qint64(t.id));
                    m.insert(QStringLiteral("title"), t.title);
                    m.insert(QStringLiteral("lang"),  t.lang);
                    m.insert(QStringLiteral("forced"), t.forced);
                    m.insert(QStringLiteral("selected"), t.selected);
                    out.append(m);
                }
                m_tracks = out;
                emit tracksChanged();
            }, Qt::QueuedConnection);
    std::fprintf(stderr, "[nuvio.qt] connections snap=%d eof=%d tracks=%d ctrl=%p\n",
                 int(okA), int(okB), int(okT), static_cast<void*>(c));
}

void MpvQuickItem::setVolumePercent(int percent)
{
    const int clamped = std::clamp(percent, 0, 130);
    if (clamped == volumePercent()) return;
    m_volume = double(clamped);
    if (m_controller)
        m_controller->enqueueCommand({QStringLiteral("set"),
                                      QStringLiteral("volume"),
                                      QString::number(clamped)});
    emit volumePercentChanged();
}

void MpvQuickItem::applySnapshot(PlaybackSnapshot snap)
{
    static std::atomic<int> firstDeliveryLog{0};
    if (firstDeliveryLog.fetch_add(1) == 0)
        std::fprintf(stderr, "[nuvio.qt] FIRST applySnapshot pos=%lf dur=%lf\n",
                     snap.positionSec, snap.durationSec);

    const bool wasMedia = m_cachedSnap.hasMedia();
    const bool mediaNow = snap.hasMedia();
    m_cachedSnap = snap;

    if (wasMedia != mediaNow)           emit hasMediaChanged();
    emit positionMsChanged();
    emit durationMsChanged();
    emit pausedChanged();
    emit bufferingChanged();
    emit hwdecCurrentChanged();
    emit cacheSecondsChanged();
}

// ---- QML API ---------------------------------------------------------------

void MpvQuickItem::play(const QString& url, const QString& audioUrl,
                        const qint64 startMs)
{
    if (!m_controller) {
        qCWarning(lcNuvioMpvCtrl, "play() before controller attached");
        return;
    }
    if (!m_ctxReady.load(std::memory_order_acquire)) {
        // mpv refuses to open the VO without a live render context; park the
        // request until notifyRenderContextReady() flushes it (exactly one
        // buffered request — latest wins).
        qCInfo(lcNuvioMpvCtrl, "parking play() until render context ready");
        m_pendingUrl    = url;
        m_pendingAudio  = audioUrl;
        m_pendingStartMs = startMs;
        return;
    }
    m_controller->loadFile(url, audioUrl, startMs);
}

void MpvQuickItem::togglePlayPause()
{
    if (!m_controller || !hasMedia()) return;
    m_controller->setPaused(!m_cachedSnap.paused);
}

/// Directive W2: raw forward onto the controller's command queue. mpv is
/// the single input authority; we never pre-filter beyond emptiness.
bool MpvQuickItem::sendKey(const QString& mpvKeyName)
{
    if (!m_controller || mpvKeyName.isEmpty()) return false;
    m_controller->enqueueCommand(QStringList{MpvKeyMap::kPress, mpvKeyName});
    return true;
}

void MpvQuickItem::setTrack(const QString& kind, const int id)
{
    if (!m_controller) return;
    const QString prop =
        (kind == QLatin1String("audio")) ? QStringLiteral("aid")
        : (kind == QLatin1String("sub"))  ? QStringLiteral("sid")
                                          : QString();
    if (prop.isEmpty()) return;
    // Explicit selection only (never alang/slang — same doctrine as the
    // auto-selector); id <= 0 disables the track class.
    m_controller->setPropertyString(
        prop, id > 0 ? QString::number(id) : QStringLiteral("no"));
}

void MpvQuickItem::setSpeed(const double speed)
{
    if (!m_controller) return;
    const double clamped = std::clamp(speed, 0.01, 100.0);
    m_controller->setPropertyString(QStringLiteral("speed"),
                                    QString::number(clamped));
}

void MpvQuickItem::seekBySeconds(double deltaSec)
{
    if (!m_controller || !hasMedia()) return;
    m_controller->seekToSeconds(deltaSec, /*absolute*/ false);
}

void MpvQuickItem::seekToSeconds(double absoluteSec)
{
    if (!m_controller || !hasMedia()) return;
    m_controller->seekToSeconds(absoluteSec, /*absolute*/ true);
}

void MpvQuickItem::stop()
{
    if (m_controller)
        m_controller->enqueueCommand({QStringLiteral("stop")});
}

QString MpvQuickItem::formatTime(double seconds) const
{ return formatTimeStatic(seconds); }

// ---- input ownership (keyboard/wheel belong to mpv during playback) --------

void MpvQuickItem::forwardKey(const QString& envelope, const QString& keyText)
{
    if (m_controller && !keyText.isEmpty())
        m_controller->enqueueCommand({envelope, keyText});
}

void MpvQuickItem::keyPressEvent(QKeyEvent* ev)
{
    // App-reserved: Esc never reaches mpv (fullscreen-exit / back navigation
    // belongs to the shell; the embedded `f`-bind is inert under libmpv).
    if (ev->key() == Qt::Key_Escape || !hasMedia()) {
        ev->ignore();
        QQuickFramebufferObject::keyPressEvent(ev);
        return;
    }
    if (const std::optional<QString> key = MpvKeyMap::textFor(ev)) {
        forwardKey(MpvKeyMap::kPress, *key);
        qCDebug(lcNuvioMpvKeys, "forward %s", key->toUtf8().constData());
        ev->accept();
        return;
    }
    QQuickFramebufferObject::keyPressEvent(ev);       // fall-through doctrine
}

void MpvQuickItem::wheelEvent(QWheelEvent* ev)
{
    if (!hasMedia()) { ev->ignore(); QQuickFramebufferObject::wheelEvent(ev); return; }
    if (const std::optional<QString> dir = MpvKeyMap::textFor(ev)) {
        forwardKey(MpvKeyMap::kPress, *dir);
        ev->accept();
        return;
    }
    QQuickFramebufferObject::wheelEvent(ev);
}

void MpvQuickItem::mousePressEvent(QMouseEvent* ev)
{
    forceActiveFocus(Qt::MouseFocusReason);   // keyboard rides surface focus
    togglePlayPause();                        // single-click convention
    ev->accept();
    QQuickFramebufferObject::mousePressEvent(ev);
}

void MpvQuickItem::componentComplete()
{
    QQuickFramebufferObject::componentComplete();
    // Take keyboard focus on mount so early input rides the surface.
    forceActiveFocus(Qt::OtherFocusReason);
}

void MpvQuickItem::mouseDoubleClickEvent(QMouseEvent* ev)
{
    emit doubleClicked();
    ev->accept();
}

// ---- cross-thread support ---------------------------------------------------

void MpvQuickItem::reportGlVendorToController(const QString& vendorLower)
{
    if (MpvController* c = m_controller.data())
        QMetaObject::invokeMethod(c, "applyGlVendorLower",
                                  Q_ARG(QString, vendorLower));
}

void MpvQuickItem::publishRenderStatsIfFirst(
    const std::weak_ptr<RenderStats>& s)
{
    if (!m_renderStats.lock()) m_renderStats = s;
}

void MpvQuickItem::notifyRenderContextReady()
{
    const bool was = m_ctxReady.exchange(true, std::memory_order_acq_rel);
    if (was) return;

    emit renderContextReady();

    // Flush exactly one parked launch now that the VO path exists.
    if (!m_pendingUrl.isEmpty() && m_controller) {
        m_controller->loadFile(m_pendingUrl, m_pendingAudio,
                               m_pendingStartMs);
    }
    m_pendingUrl.clear();
    m_pendingAudio.clear();
}

void MpvQuickItem::requestUpdate()
{
    update();   // schedules a scenegraph frame (mpv's wake = only trigger)
}

} // namespace nuvio::mpv