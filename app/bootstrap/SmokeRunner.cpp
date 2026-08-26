#include "bootstrap/SmokeRunner.h"

#include "bootstrap/LogCategories.h"
#include "nuvio/mpv/MpvController.h"
#include "nuvio/mpv/MpvQuickItem.h"
#include "nuvio/mpv/MpvRenderer.h"   // RenderStats definition

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

namespace nuvio::app {

namespace {
constexpr int kRequiredAdvances = 3;
constexpr int kGraceMs          = 4000;
} // namespace

bool SmokeRunner::requested(Config* out)
{
    const QString url = qEnvironmentVariable("NUVIO_QT_SMOKE_URL");
    if (url.isEmpty()) return false;

    Config c;
    c.url = url;
    bool ok = false;
    const int v = qEnvironmentVariable("NUVIO_QT_SMOKE_TIMEOUT").toInt(&ok);
    if (ok && v > 0) c.timeoutSeconds = v;
    if (out) *out = c;
    return true;
}

void SmokeRunner::begin(QObject* itemObj, QObject* controllerObj,
                        const Config& cfg)
{
    auto*             item = qobject_cast<nuvio::mpv::MpvQuickItem*>(itemObj);
    auto*             ctrl = qobject_cast<nuvio::mpv::MpvController*>(controllerObj);
    QCoreApplication* app  = QCoreApplication::instance();
    if (!item || !ctrl || !app) {
        std::fprintf(stderr,
                     "NUVIO-FATAL: smoke wiring failed (item=%p ctrl=%p)\n",
                     static_cast<void*>(item), static_cast<void*>(ctrl));
        emit finished(false, QStringLiteral("wiring"));
        if (app) app->exit(1);
        return;
    }

    qCInfo(lcNuvioAppStart).nospace()
        << "SMOKE: target='" << cfg.url
        << "' timeout=" << cfg.timeoutSeconds << 's';

    // ---- LIFETIME CONTRACT (the ASan lesson of P0 day one) -----------------
    // Every lambda here may outlive begin(). Captures are exclusively:
    // shared_ptr state, a QPointer-guarded harness, and QObject pointers
    // owned elsewhere (engine/app). By-reference capture of this frame is
    // BANNED — it is precisely the bug class that segfaulted 3/3 runs.
    struct State {
        bool   done      = false;
        int    advances  = 0;
        qint64 lastPosMs = -1;
    };
    auto st                     = std::make_shared<State>();
    const QPointer<SmokeRunner> self(this);
    const int timeoutMs         = cfg.timeoutSeconds * 1000;   // value NOW

    auto finish = [st, self, item, app, ctrl](bool pass, const QString& why) {
        if (st->done) return;
        st->done = true;

        // Ground truth recorded on the event thread (plain read, no invoke).
        const QString core = ctrl->debugCoreState();

        const auto s = item->snapshotPublic();
        const QString stats =
            QStringLiteral("pos=%1s dur=%2s hwdec=%3 eof=%4 advances=%5 | CORE %6")
                .arg(s.positionSec, 0, 'f', 2)
                .arg(s.durationSec, 0, 'f', 2)
                .arg(s.hwdecCurrent.isEmpty() ? QStringLiteral("?")
                                              : s.hwdecCurrent)
                .arg(s.eofReached ? 1 : 0)
                .arg(st->advances)
                .arg(core);
        if (self) {
            emit self->finished(pass, why);
            std::fprintf(stderr, "SMOKE RESULT: %s (%s) %s\n",
                         pass ? "PASS" : "FAIL", why.toUtf8().constData(),
                         stats.toUtf8().constData());
            qCInfo(lcNuvioAppStart).noquote()
                << (pass ? "SMOKE RESULT: PASS (" : "SMOKE RESULT: FAIL (")
                << why << QLatin1Char(')') << stats;
        }
        app->exit(pass ? 0 : 1);
    };

    // Natural EOF counts as PASS (QueuedConnection: crosses controller thread).
    QObject::connect(ctrl, &nuvio::mpv::MpvController::reachedEnd, item,
                     [st, finish] {
                         if (!st->done) finish(true, QStringLiteral("eof"));
                     },
                     Qt::QueuedConnection);

    // Init failure is an immediate, honest FAIL (never a silent timeout).
    QObject::connect(ctrl, &nuvio::mpv::MpvController::ready, item,
                     [st, finish](bool ok, const QString& err) {
                         if (!ok && !st->done)
                             finish(false,
                                    QStringLiteral("init: %1").arg(err));
                     },
                     Qt::QueuedConnection);

    // Position-advance sampler — armed after the buffering grace window.
    auto* sampler = new QTimer(item);
    sampler->setInterval(250);
    QObject::connect(sampler, &QTimer::timeout, item,
                     [st, item, finish] {
                         if (st->done) return;
                         const auto s = item->snapshotPublic();
                         if (s.positionSec < 0) return;
                         const qint64 posMs =
                             qint64(s.positionSec * 1000.0);
                         if (st->lastPosMs >= 0 && posMs != st->lastPosMs)
                             ++st->advances;
                         st->lastPosMs = posMs;
                         if (st->advances >= kRequiredAdvances &&
                             s.durationSec > 0)
                             finish(true, QStringLiteral("advancing"));
                     });

    // Human-readable heartbeat for triage of slow starts / stalls.
    auto* beat = new QTimer(item);
    beat->setInterval(1000);
    QObject::connect(beat, &QTimer::timeout, item, [st, item, ctrl] {
        if (st->done) return;
        const auto s     = item->snapshotPublic();
        const auto stats = item->renderStats();
        qCInfo(lcNuvioAppStart).nospace().noquote()
            << "SMOKEBEAT pos=" << s.positionSec << "s dur=" << s.durationSec
            << "s cache=" << s.demuxerCacheSec
            << " stall=" << (s.pausedForCache ? 1 : 0)
            << " loaded=" << (s.fileLoaded ? 1 : 0)
            << " ready=" << (ctrl->isReady() ? 1 : 0)
            << " hasMedia=" << (item->hasMedia() ? 1 : 0)
            << " glFrames=" << (stats ? int(stats->publishedFrames.load())
                                      : -1);
    });
    beat->start();

    // Hard ceiling regardless of anything else.
    QTimer::singleShot(timeoutMs, item, [st, finish] {
        if (!st->done) finish(false, QStringLiteral("timeout"));
    });

    // Grace before counting so pre-buffer silence isn't punished.
    QTimer::singleShot(kGraceMs, item, [sampler] { sampler->start(); });

    // Launch (URL deep-copied into the controller's queued command).
    item->play(cfg.url);
}

} // namespace nuvio::app
