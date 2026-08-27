#include "nuvio/mpv/MpvController.h"

#include "nuvio/mpv/HwdecPolicy.h"
#include "nuvio/mpv/MpvLog.h"
#include "nuvio/mpv/MpvUserConfig.h"

#include <QLoggingCategory>
#include <QVariant>

#include <mpv/client.h>

#include <chrono>
#include <cstdio>
#include <cstdio>
#include <vector>

namespace nuvio::mpv {

namespace {
constexpr qint64 kDefaultDemuxerForwardBytes = 256ll * 1024 * 1024;

constexpr int kObserveTimePos      = 1;
constexpr int kObserveDuration     = 2;
constexpr int kObserveSpeed        = 3;
constexpr int kObserveVolume       = 4;
constexpr int kObservePause        = 5;
constexpr int kObserveEof          = 6;
constexpr int kObserveCacheStalled = 7;
constexpr int kObserveCacheTime    = 8;
constexpr int kObserveSeekable     = 9;
constexpr int kObserveHwdec        = 10;
constexpr int kObserveTrackList    = 11;

constexpr qint64 kPublishMinIntervalMs = 66;            // ~15 Hz UI throttle

// Vendor-neutral decode-failure markers for the sticky auto-fallback.
constexpr const char* kDecodeFailMarkers[] = {
    "Mapping hardware decoded surface failed",
    "Failed to import surface in EGL",
};

qint64 wallMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

MpvController::MpvController(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<PlaybackSnapshot>("nuvio::mpv::PlaybackSnapshot");
    qRegisterMetaType<TrackInfo>("nuvio::mpv::TrackInfo");
    qRegisterMetaType<QVector<TrackInfo>>("QVector<nuvio::mpv::TrackInfo>");
}

MpvController::~MpvController() { shutdownAndWait(); }

void MpvController::start()
{
    if (m_thread.joinable()) return;
    m_stop.store(false, std::memory_order_relaxed);
    m_joined.store(false, std::memory_order_relaxed);
    std::thread t([this] { threadMain(); });
    m_thread.swap(t);
}

void MpvController::shutdownAndWait()
{
    if (m_joined.exchange(true)) return;
    m_stop.store(true, std::memory_order_release);
    if (m_handle) mpv_wakeup(m_handle);          // any-thread-safe per API
    if (m_thread.joinable()) m_thread.join();    // unconditional join
    if (m_handle) { mpv_terminate_destroy(m_handle); m_handle = nullptr; }
    m_ready.store(false, std::memory_order_release);
}

mpv_handle* MpvController::handleForRenderInit() { return m_handle; }

void MpvController::attachRenderStats(std::weak_ptr<RenderStats> s)
{ m_renderStats = std::move(s); }

PlaybackSnapshot MpvController::snapshot() const
{
    std::lock_guard<std::mutex> g(m_snapMutex);
    return m_snapshot;
}

void MpvController::enqueueCommand(const QStringList& args)
{
    {
        std::lock_guard<std::mutex> g(m_cmdMutex);
        m_cmdQueue.push_back(args);
    }
    if (m_handle) mpv_wakeup(m_handle);
}

void MpvController::setPropertyString(const QString& n, const QString& v)
{ enqueueCommand({QStringLiteral("set"), n, v}); }

void MpvController::loadFile(const QString& url, const QString& audioFile,
                             qint64 startMillis)
{
    qCInfo(lcNuvioMpvCtrl, "loadfile '%s' audio=%s start=%lldms",
           url.toUtf8().constData(),
           audioFile.isEmpty() ? "<same>" : audioFile.toUtf8().constData(),
           static_cast<long long>(startMillis));
    if (!audioFile.isEmpty())
        setPropertyString(QStringLiteral("audio-file"), audioFile);
    if (startMillis > 0)
        setPropertyString(QStringLiteral("play-start"),
                          QString::number(startMillis / 1000.0));
    enqueueCommand({QStringLiteral("loadfile"), url});
    setPaused(false);
}

void MpvController::setPaused(bool p)
{ enqueueCommand({QStringLiteral("set"), QStringLiteral("pause"),
                  p ? QStringLiteral("yes") : QStringLiteral("no")}); }

void MpvController::seekToSeconds(double s, bool absolute)
{ enqueueCommand({QStringLiteral("seek"), QString::number(s, 'f', 3),
                  absolute ? QStringLiteral("absolute")
                           : QStringLiteral("relative")}); }

void MpvController::setVolumePercent(double pct)
{ enqueueCommand({QStringLiteral("set"), QStringLiteral("volume"),
                  QString::number(pct)}); }

void MpvController::setSpeedFactor(double f)
{ enqueueCommand({QStringLiteral("set"), QStringLiteral("speed"),
                  QString::number(f)}); }

void MpvController::cycleMute()
{ enqueueCommand({QStringLiteral("cycle"), QStringLiteral("mute")}); }

void MpvController::applyGlVendorLower(const QString& vendorLower)
{
    if (m_vendorApplied.exchange(true)) return;
    const QString chain = HwdecPolicy::selectChain(
        vendorLower, HwdecPolicy::nvidiaDetectedBySystem());
    qCInfo(lcNuvioMpvPolicy, "hwdec refined by GL vendor '%s' -> '%s'%s",
           vendorLower.toUtf8().constData(), chain.toUtf8().constData(),
           HwdecPolicy::userHwdecOverride().isEmpty()
               ? "" : " [env override active]");
    executeOnThread({QStringLiteral("set"), QStringLiteral("hwdec"), chain});
}

QString MpvController::copyModeOf(const QString& chain)
{
    const QString first = chain.section(u',', 0, 0).trimmed();
    if (first.endsWith(QLatin1String("-copy")) || first == QLatin1String("no"))
        return first;
    return first + QStringLiteral("-copy");
}

void MpvController::maybeAutoFallbackDecode(const QByteArray& text, int level)
{
    if (m_noAutoFallback.load(std::memory_order_relaxed) || m_fallbackSticky)
        return;
    if (level > MPV_LOG_LEVEL_ERROR) return;

    bool hit = false;
    for (const auto* marker : kDecodeFailMarkers)
        if (text.contains(marker)) { hit = true; break; }
    if (!hit) return;

    const qint64 now = wallMs();
    if (m_failStreak == 0 || now - m_streakStartMs > 2000) {
        m_failStreak    = 0;
        m_streakStartMs = now;
    }
    if (++m_failStreak >= 8 && m_handle && !m_activeChain.isEmpty()) {
        const QString copy = copyModeOf(m_activeChain);
        m_failStreak     = 0;
        m_fallbackSticky = true;                       // sticky per instance
        qCWarning(lcNuvioMpvPolicy,
                  "decode-surface failure streak -> sticky fallback '%s'",
                  copy.toUtf8().constData());
        executeOnThread({QStringLiteral("set"), QStringLiteral("hwdec"), copy});
    }
}


bool MpvController::initializeCoreOnThread(QString* errOut)
{
    m_handle = mpv_create();
    if (!m_handle) { *errOut = QStringLiteral("mpv_create failed"); return false; }

    auto setOpt = [&](const char* name, const char* value) -> bool {
        const int rc = mpv_set_option_string(m_handle, name, value);
        if (rc < 0) {
            *errOut = QStringLiteral("option %1=%2 rc=%3")
                          .arg(QString::fromLatin1(name),
                               QString::fromLatin1(value))
                          .arg(rc);
            return false;
        }
        return true;
    };

    // ---- USER config FIRST (values win; our forced block after reclaims
    //      only render-API structural requirements). Keyboard ownership is
    //      configured inside apply(): default bindings + explicit input-conf.
    QString err;
    const UserConfig user = MpvUserConfig::discover();
    qCInfo(lcNuvioMpvConf, "user config: mpv.conf=%s input.conf=%s",
           user.mpvConfFound ? user.mpvConfPath.toUtf8().constData() : "<none>",
           user.inputConfPath.isEmpty()
               ? "<none>" : user.inputConfPath.toUtf8().constData());
    if (!MpvUserConfig::apply(m_handle, user, &err)) {
        *errOut = QStringLiteral("user-config apply: %1").arg(err);
        return false;
    }

    // ---- Forced baseline ---------------------------------------------------
    m_baseChain = HwdecPolicy::selectChain({}, HwdecPolicy::nvidiaDetectedBySystem());
    m_noAutoFallback.store(
        qEnvironmentVariableIntValue("NUVIO_MPV_NO_AUTOFALLBACK") == 1,
        std::memory_order_relaxed);

    struct Opt { const char* k; QByteArray v; };
    const Opt opts[] = {
        {"vo", "libmpv"},
        {"idle", "yes"},
        // Embedding contract: the host owns windowing; mpv must NOT try to
        // open any VO before mpv_render_context_create (playback requests
        // park until notifyRenderContextReady). Forcing a window here races
        // the renderer and fails VO init on async hosts/offscreen CI.
        {"force-window", "no"},
        {"keep-open", "no"},
        {"terminal", "no"},
        {"audio-display", "no"},
        {"osd-level", "0"},
        {"osc", "no"},
        {"hwdec", m_baseChain.toUtf8()},
        {"demuxer-max-bytes",
             QByteArray::number(kDefaultDemuxerForwardBytes)},
        // Back buffer must NOT mirror forward (doubling bug, AGENTS.md):
        {"demuxer-max-back-bytes",
             QByteArray::number(demuxerBackBufferBytes(kDefaultDemuxerForwardBytes))},
    };
    for (const auto& o : opts)
        if (!setOpt(o.k, o.v.constData())) return false;

    // ---- Optional tester/CI escape hatch (documented env knob) -------------
    // NUVIO_MPV_EXTRA_OPTS="key=value,key2=value2" — appended AFTER forced
    // baseline so it wins; unknown names are non-fatal (mpv rc logged).
    const QByteArray extraOpts = qgetenv("NUVIO_MPV_EXTRA_OPTS");
    if (!extraOpts.isEmpty()) {
        for (const QByteArray& pair : extraOpts.split(',')) {
            const int eq = pair.indexOf('=');
            if (eq <= 0) continue;
            const QString k = QString::fromUtf8(pair.left(eq));
            const QString v = QString::fromUtf8(pair.mid(eq + 1));
            qCInfo(lcNuvioMpvCtrl, "extra opt %s=%s",
                   k.toUtf8().constData(), v.toUtf8().constData());
            mpv_set_option_string(m_handle, k.toUtf8().constData(),
                                  v.toUtf8().constData());
        }
    }

    // video-sync deliberately UNSET: stock audio-sync IS the directive;

    const int rc = mpv_initialize(m_handle);
    if (rc < 0) { *errOut = QStringLiteral("mpv_initialize rc=%1").arg(rc); return false; }

    mpv_request_log_messages(
        m_handle, qEnvironmentVariableIntValue("NUVIO_MPV_DEBUG") == 1 ? "info"
                                                                       : "warn");

#define NUVIO_OBS(id, name, fmt) \
    mpv_observe_property(m_handle, id, name, fmt)
    NUVIO_OBS(kObserveTimePos,      "time-pos",           MPV_FORMAT_DOUBLE);
    NUVIO_OBS(kObserveDuration,     "duration",           MPV_FORMAT_DOUBLE);
    NUVIO_OBS(kObserveSpeed,        "speed",              MPV_FORMAT_DOUBLE);
    NUVIO_OBS(kObserveVolume,       "volume",             MPV_FORMAT_DOUBLE);
    NUVIO_OBS(kObservePause,        "pause",              MPV_FORMAT_FLAG);
    NUVIO_OBS(kObserveEof,          "eof-reached",        MPV_FORMAT_FLAG);
    NUVIO_OBS(kObserveCacheStalled, "paused-for-cache",   MPV_FORMAT_FLAG);
    NUVIO_OBS(kObserveCacheTime,    "demuxer-cache-time", MPV_FORMAT_DOUBLE);
    NUVIO_OBS(kObserveSeekable,     "seekable",           MPV_FORMAT_FLAG);
    NUVIO_OBS(kObserveHwdec,        "hwdec-current",      MPV_FORMAT_STRING);
    NUVIO_OBS(kObserveTrackList,    "track-list",         MPV_FORMAT_NODE);
#undef NUVIO_OBS

    m_activeChain = m_baseChain;
    return true;
}

void MpvController::executeOnThread(const QStringList& args)
{
    QVector<QByteArray> storage;
    storage.reserve(args.size());
    for (const QString& a : args) storage << a.toUtf8();

    std::vector<const char*> argv;
    argv.reserve(size_t(storage.size() + 1));
    for (const auto& b : storage) argv.push_back(b.constData());
    argv.push_back(nullptr);

    const int rc = mpv_command(m_handle, const_cast<const char**>(argv.data()));
    if (rc < 0)
        qCDebug(lcNuvioMpvCtrl, "command '%s' rc=%d",
                args.value(0).toUtf8().constData(), rc);
}

void MpvController::threadMain()
{
    QString err;
    if (!initializeCoreOnThread(&err)) {
        // Gate visibility rule: raw stderr, always visible.
        std::fprintf(stderr, "NUVIO-FATAL: mpv core init failed: %s\n",
                     err.toUtf8().constData());
        if (m_handle) { mpv_terminate_destroy(m_handle); m_handle = nullptr; }
        emit ready(false, err);
        return;
    }
    m_ready.store(true, std::memory_order_release);
    qCInfo(lcNuvioMpvCtrl, "mpv core initialized");
    emit ready(true, {});

    while (!m_stop.load(std::memory_order_acquire)) {
        // 0) Core-clock mirror: some distro libmpv builds deliver
        //    double-property observations unreliably under embedders; poll
        //    the two authoritative numbers directly at loop cadence
        //    (bounded by the 250 ms slice — this is diagnostics/state ONLY,
        //    it schedules nothing).
        {
            double v = 0;
            bool dirty = false;
            int rc = mpv_get_property(m_handle, "time-pos", MPV_FORMAT_DOUBLE, &v);
            m_dbgTpRc.store(rc, std::memory_order_relaxed);
            m_dbgTpValX100.store(llround(v * 100.0), std::memory_order_relaxed);
            if (rc >= 0 && v >= 0) {
                std::lock_guard<std::mutex> g(m_snapMutex);
                if (m_snapshot.positionSec != v) { m_snapshot.positionSec = v; dirty = true; }
            }
            rc = mpv_get_property(m_handle, "duration", MPV_FORMAT_DOUBLE, &v);
            m_dbgDurRc.store(rc, std::memory_order_relaxed);
            if (rc >= 0 && v > 0) {
                std::lock_guard<std::mutex> g(m_snapMutex);
                if (m_snapshot.durationSec != v) { m_snapshot.durationSec = v; dirty = true; }
            }
            int f = -2;
            if (mpv_get_property(m_handle, "pause", MPV_FORMAT_FLAG, &f) >= 0)
                m_dbgPauseFlag.store(f, std::memory_order_relaxed);
            char* idleS = mpv_get_property_string(m_handle, "idle-active");
            if (idleS) {
                m_dbgIdleActive.store(QString::fromUtf8(idleS).toInt(),
                                      std::memory_order_relaxed);
                mpv_free(idleS);
            }
            // Mirror touches the shared cache the UI consumes — republish
            // (publishSnapshotPlayback coalesces at ~15 Hz).
            if (dirty) publishSnapshotPlayback();
        }

        // 1) drain queued commands on THIS thread — single-owner doctrine.
        for (;;) {
            QStringList next;
            {
                std::lock_guard<std::mutex> g(m_cmdMutex);
                if (m_cmdQueue.empty()) break;
                next = std::move(m_cmdQueue.front());
                m_cmdQueue.pop_front();
            }
            executeOnThread(next);
        }

        // 2) bounded event slice; early wake via mpv_wakeup.
        mpv_event* ev = mpv_wait_event(m_handle, 0.25);
        if (ev && ev->event_id != MPV_EVENT_NONE) processEvent(ev);
    }

    qCInfo(lcNuvioMpvCtrl, "event thread exiting");
}

namespace {
QVariant nodeToVariant(const mpv_node* n)
{
    if (!n) return {};
    switch (n->format) {
    case MPV_FORMAT_STRING:  return QString::fromUtf8(n->u.string);
    case MPV_FORMAT_FLAG:    return n->u.flag != 0;
    case MPV_FORMAT_INT64:   return qlonglong(n->u.int64);
    case MPV_FORMAT_DOUBLE:  return n->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList l;
        const auto* arr = n->u.list;
        if (arr)
            for (int i = 0; i < arr->num; ++i)
                l << nodeToVariant(&arr->values[i]);
        return l;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap m;
        const auto* lst = n->u.list;
        if (lst)
            for (int i = 0; i < lst->num; ++i)
                m.insert(QString::fromUtf8(lst->keys ? lst->keys[i] : ""),
                         nodeToVariant(&lst->values[i]));
        return m;
    }
    default: return {};
    }
}
} // namespace

void MpvController::processEvent(mpv_event* ev)
{
    switch (ev->event_id) {
    case MPV_EVENT_SHUTDOWN:
        qCWarning(lcNuvioMpvCtrl, "core shutdown observed");
        m_stop.store(true, std::memory_order_release);
        break;

    case MPV_EVENT_LOG_MESSAGE: {
        auto* msg  = static_cast<mpv_event_log_message*>(ev->data);
        const QByteArray text(msg->text);
        const int level = msg->log_level;
        // warn/error/fatal always reach stderr regardless of filters
        // (parity with the bridge line's visibility promise).
        if (level <= MPV_LOG_LEVEL_WARN)
            std::fprintf(stderr, "[nuvio.mpv/%s] %s",
                         msg->prefix ? msg->prefix : "mpv", text.constData());
        else if (lcNuvioMpvLog().isDebugEnabled())
            qCDebug(lcNuvioMpvLog, "%s", text.constData());
        maybeAutoFallbackDecode(text, level);
        break;
    }

    case MPV_EVENT_FILE_LOADED: {
        m_failStreak = 0;                       // reset per file (doctrine)
        {
            std::lock_guard<std::mutex> g(m_snapMutex);
            m_snapshot.fileLoaded = true;
            m_snapshot.eofReached = false;
        }
        emit fileLoaded();
        publishSnapshotPlayback();
        break;
    }

    case MPV_EVENT_END_FILE: {
        auto* endf = static_cast<mpv_event_end_file*>(ev->data);
        if (endf && endf->reason == MPV_END_FILE_REASON_EOF) {
            {
                std::lock_guard<std::mutex> g(m_snapMutex);
                m_snapshot.eofReached = true;
            }
            emit reachedEnd();                  // idle=yes keeps core alive
            publishSnapshotPlayback();
        }
        break;
    }

    case MPV_EVENT_PROPERTY_CHANGE: {
        auto* prop = static_cast<mpv_event_property*>(ev->data);
        if (!prop || !prop->name) break;

        // Copy-modify-write keeps the cache lock scope tiny; publishing
        // happens strictly outside the lock (no recursive locking).
        PlaybackSnapshot s;
        {   std::lock_guard<std::mutex> g(m_snapMutex); s = m_snapshot; }
        bool dirty     = false;
        bool flagDirty = false;   // latency-critical flips publish urgently

        if (std::strcmp(prop->name, "time-pos") == 0 &&
            prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            s.positionSec = *static_cast<double*>(prop->data); dirty = true;
        } else if (std::strcmp(prop->name, "duration") == 0 &&
                   prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            s.durationSec = *static_cast<double*>(prop->data); dirty = true;
        } else if (std::strcmp(prop->name, "speed") == 0 &&
                   prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            s.speed = *static_cast<double*>(prop->data); dirty = true;
        } else if (std::strcmp(prop->name, "volume") == 0 &&
                   prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            s.volume = *static_cast<double*>(prop->data); dirty = true;
        } else if (std::strcmp(prop->name, "demuxer-cache-time") == 0 &&
                   prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            s.demuxerCacheSec = *static_cast<double*>(prop->data); dirty = true;
        } else if (prop->format == MPV_FORMAT_FLAG && prop->data) {
            const bool v = *static_cast<int*>(prop->data) != 0;
            if (std::strcmp(prop->name, "pause") == 0) {
                if (v != s.paused) {
                    s.paused   = v;
                    dirty      = true;
                    flagDirty  = true;
                }
            } else if (std::strcmp(prop->name, "eof-reached") == 0) {
                if (v != s.eofReached) {
                    s.eofReached = v;
                    dirty        = true;
                    flagDirty    = true;
                }
            } else if (std::strcmp(prop->name, "paused-for-cache") == 0) {
                if (v != s.pausedForCache) {
                    s.pausedForCache = v;
                    dirty            = true;
                    flagDirty        = true;
                }
            } else if (std::strcmp(prop->name, "seekable") == 0) {
                if (v != s.seekable) { s.seekable = v; dirty = true; }
            }
        } else if (std::strcmp(prop->name, "hwdec-current") == 0 &&
                   prop->format == MPV_FORMAT_STRING && prop->data) {
            // Event data is char** here — dereference exactly once.
            const auto* str = static_cast<char**>(prop->data);
            const QString cur =
                (str && *str) ? QString::fromUtf8(*str) : QString();
            if (cur != m_prevHwdec) {
                const bool degraded = !m_prevHwdec.isEmpty() &&
                                      !m_prevHwdec.contains(u"copy") &&
                                      cur.contains(u"-copy");
                // Doctrine: decoder transitions ALWAYS print — change-only.
                std::fprintf(stderr, "decoder: %s: %s\n",
                             m_prevHwdec.isEmpty()  ? "attached"
                             : degraded             ? "degraded"
                                                    : "changed",
                             cur.toUtf8().constData());
                m_prevHwdec    = cur;
                s.hwdecCurrent = cur;
                dirty          = true;
                QMetaObject::invokeMethod(
                    this,
                    [this, cur] { emit decoderChanged(cur); },
                    Qt::QueuedConnection);
            }
        } else if (std::strcmp(prop->name, "track-list") == 0 &&
                   prop->format == MPV_FORMAT_NODE && prop->data) {
            const QVariant v = nodeToVariant(static_cast<mpv_node*>(prop->data));
            QVector<TrackInfo> tracks;
            if (v.metaType().id() == QMetaType::QVariantList) {
                const auto list = v.toList();
                tracks.reserve(list.size());
                for (const auto& entry : list) {
                    const auto map = entry.toMap();
                    TrackInfo t;
                    t.id    = map.value(QStringLiteral("id")).toLongLong();
                    const QString kind =
                        map.value(QStringLiteral("type")).toString();
                    if      (kind == QLatin1String("audio"))
                        t.kind = TrackKind::Audio;
                    else if (kind == QLatin1String("video"))
                        t.kind = TrackKind::Video;
                    else if (kind == QLatin1String("sub"))
                        t.kind = TrackKind::Subtitle;
                    else    t.kind = TrackKind::Other;
                    t.title  = map.value(QStringLiteral("title")).toString();
                    t.lang   = map.value(QStringLiteral("lang")).toString();
                    t.codec  = map.value(QStringLiteral("codec")).toString();
                    t.def    = map.value(QStringLiteral("default")).toBool();
                    t.forced = map.value(QStringLiteral("forced")).toBool();
                    tracks << t;
                }
            }
            QMetaObject::invokeMethod(
                this, [this, tracks] { emit trackListChanged(tracks); },
                Qt::QueuedConnection);
        }

        if (dirty) {
            {   std::lock_guard<std::mutex> g(m_snapMutex); m_snapshot = s; }
            // Publish here too: FILE_LOADED alone proved insufficient to
            // resume streaming once early deliveries coalesced away. Flag
            // flips bypass the rate limiter — they are exactly the events a
            // silence gap would otherwise strand (see header note).
            publishSnapshotPlayback(flagDirty);
        }
        break;
    }

    default:
        break;
    }
}

QString MpvController::debugCoreState()
{
    // Reads atomics + the shared cache — no cross-thread invocation.
    double cachedPos = -999, cachedDur = -999;
    {
        std::lock_guard<std::mutex> g(m_snapMutex);
        cachedPos = m_snapshot.positionSec;
        cachedDur = m_snapshot.durationSec;
    }
    const int loaded =
        [&] {
             std::lock_guard<std::mutex> g(m_snapMutex);
             return m_snapshot.fileLoaded ? 1 : 0;
         }();
    return QStringLiteral(
               "tpV=%8 tpRc=%1 durRc=%2 pause=%3 idle=%4 | cachePos=%5 cacheDur=%6 "
               "| fileLoaded=%7 pubs=%8")
        .arg(m_dbgTpRc.load())
        .arg(m_dbgDurRc.load())
        .arg(m_dbgPauseFlag.load())
        .arg(m_dbgIdleActive.load())
        .arg(cachedPos, 0, 'f', 2)
        .arg(cachedDur, 0, 'f', 2)
        .arg(loaded)
        .arg(m_dbgPubs.load()).arg(m_dbgTpValX100.load());
}

void MpvController::publishSnapshotPlayback(const bool urgent)
{
    const qint64 now = wallMs();
    if (!urgent && now - m_lastPublishMs < kPublishMinIntervalMs)
        return;                       // coalesced; urgent paths emit directly
    m_lastPublishMs = now;
    emit snapshotChanged(snapshot());
    m_dbgPubs.fetch_add(1, std::memory_order_relaxed);
}

} // namespace nuvio::mpv