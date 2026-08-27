#include "bootstrap/SmokeRunner.h"

#include "bootstrap/LogCategories.h"
#include "nuvio/mpv/MpvController.h"
#include "nuvio/mpv/MpvQuickItem.h"
#include "nuvio/mpv/MpvRenderer.h"   // RenderStats definition

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>

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

    // Directive-W2 acceptance: scratch MPV_HOME must exist BEFORE the
    // controller initializes (this runs ahead of controller->start()).
    if (qEnvironmentVariableIsSet("NUVIO_QT_KEYTEST")) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path home =
            fs::temp_directory_path(ec) / "nuvio-qt-keytest-home";
        fs::create_directories(home, ec);

        // One comment line: a present mpv.conf takes the user-config branch,
        // which is what enables the explicit input-conf pointing to this dir.
        { std::ofstream f(home / "mpv.conf", std::ios::trunc);
          f << "# keytest fixture (harness-owned)\n"; }

        // Custom bind mpv would never guess: absolute seek to 30 s.
        { std::ofstream f(home / "input.conf", std::ios::trunc);
          f << "F7 seek 30 absolute\n"; }

        qputenv("MPV_HOME", QByteArray::fromStdString(home.string()));
        c.keyTest = true;
        if (c.timeoutSeconds < 40) c.timeoutSeconds = 40;   // staged sequence
        std::fprintf(stderr,
                     "KEYTEST-SETUP: MPV_HOME=%s (input.conf F7=seek30)\n",
                     home.string().c_str());
    }

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
        // keytest staged-sequence bookkeeping (directive W2)
        bool   keyStarted = false;
        bool   spPauseOk  = false;
        bool   spResumeOk = false;
        bool   rightOk    = false;
        bool   f7Ok       = false;
        double prePos     = -1.0;
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

    // ---- directive-W2 staged sequence (engine path) -------------------------
    // Space → pause, Space → resume, Right → default +10 seek, F7 → the
    // scratch input.conf custom bind (abs seek 30). Judged strictly; every
    // step reads ground truth from the item snapshot afterwards.
    //
    // Self-reference note: the chain re-arms itself through singleShot, so
    // the callable must live behind a shared_ptr — capturing the local by
    // value would snapshot an empty std::function.
    auto keyStepPtr = std::make_shared<std::function<void(int)>>();
    auto sendK = [ctrl](const char* k) {
        ctrl->enqueueCommand({QStringLiteral("keypress"),
                              QString::fromLatin1(k)});
    };
    auto keyFinish = [st, finish](bool pass) {
        finish(pass,
               QStringLiteral("keytest pause:%1 resume:%2 right:%3 f7:%4")
                   .arg(st->spPauseOk ? 1 : 0)
                   .arg(st->spResumeOk ? 1 : 0)
                   .arg(st->rightOk ? 1 : 0)
                   .arg(st->f7Ok ? 1 : 0));
    };
    *keyStepPtr = [st, item, keyStepPtr, keyFinish, sendK](int n) {
        if (st->done) return;
        const auto s = item->snapshotPublic();
        switch (n) {
        case 0:
            st->prePos = s.positionSec;
            sendK("Space");
            QTimer::singleShot(900, item,
                               [keyStepPtr] { (*keyStepPtr)(1); });
            return;
        case 1:
            st->spPauseOk = s.paused;
            if (!s.paused)
                std::fprintf(stderr, "KEYTEST-FAIL: Space did not pause\n");
            sendK("Space");
            QTimer::singleShot(800, item,
                               [keyStepPtr] { (*keyStepPtr)(2); });
            return;
        case 2:
            st->spResumeOk = !s.paused;
            if (s.paused)
                std::fprintf(stderr,
                             "KEYTEST-FAIL: second Space did not resume\n");
            st->prePos = s.positionSec;
            sendK("Right");
            QTimer::singleShot(1600, item,
                               [keyStepPtr] { (*keyStepPtr)(3); });
            return;
        case 3:
            st->rightOk = s.positionSec >= st->prePos + 5.0;
            if (!st->rightOk)
                std::fprintf(stderr,
                             "KEYTEST-FAIL: Right no-seek (%.2f -> %.2f)\n",
                             st->prePos, s.positionSec);
            sendK("F7");
            QTimer::singleShot(2200, item,
                               [keyStepPtr] { (*keyStepPtr)(4); });
            return;
        case 4:
            st->f7Ok = qAbs(s.positionSec - 30.0) <= 8.0;
            if (!st->f7Ok)
                std::fprintf(stderr,
                             "KEYTEST-FAIL: F7 custom bind landed at %.2f "
                             "(want ~30)\n", s.positionSec);
            keyFinish(st->spPauseOk && st->spResumeOk && st->rightOk &&
                      st->f7Ok);
            return;
        }
    };

    // Position-advance sampler — armed after the buffering grace window.
    auto* sampler = new QTimer(item);
    sampler->setInterval(250);
    QObject::connect(sampler, &QTimer::timeout, item,
                     [st, item, finish, keyStepPtr, keyTest = cfg.keyTest] {
                         if (st->done) return;
                         const auto s = item->snapshotPublic();
                         if (s.positionSec < 0) return;
                         const qint64 posMs =
                             qint64(s.positionSec * 1000.0);
                         if (st->lastPosMs >= 0 && posMs != st->lastPosMs)
                             ++st->advances;
                         st->lastPosMs = posMs;
                         if (keyTest) {
                             // Keytest: normal PASS is superseded by the
                             // staged sequence; the first real advance arms it.
                             if (!st->keyStarted && st->advances >= 1 &&
                                 s.durationSec > 0) {
                                 st->keyStarted = true;
                                 (*keyStepPtr)(0);
                             }
                             return;
                         }
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
