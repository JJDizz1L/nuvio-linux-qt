// Objective P0 gate runner (plan §8). ACTIVE ONLY when NUVIO_QT_SMOKE_URL is
// set — a lifecycle harness, NOT a media scheduler: it observes the mpv
// system's own outputs at human cadence and exits machine-readable so any CI
// box (offscreen llvmpipe included) can run the identical gate.
//
// PASS inside timeout ⇢ duration known AND ≥3 distinct position advances,
//                       OR a natural EOF was reached.
#pragma once

#include <QObject>
#include <QString>

namespace nuvio::app {

class SmokeRunner final : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString url;
        int     timeoutSeconds = 25;     // NUVIO_QT_SMOKE_TIMEOUT overrides
    };

    /// True + fills cfg when NUVIO_QT_SMOKE_URL is present.
    [[nodiscard]] static bool requested(Config* outConfig);

    /** Wires observers onto a live MpvQuickItem; owns nothing. */
    void begin(QObject* mpvItemObj, QObject* controllerObj, const Config& cfg);

signals:
    /** Human-readable PASS/FAIL reason; process exit code set alongside. */
    void finished(bool pass, QString reason);
};

} // namespace nuvio::app
