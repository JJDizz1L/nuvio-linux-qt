// Per-system logging categories — HEADER-INLINE form.
//
// Rationale: category accessor *functions* defined in a separate TU inside a
// static library hit GNU ld's single-pass archive limitation when consumers
// precede the defining member. Inline functions emit weak symbols in every
// consuming TU — immune to ordering, portable across linkers (§7.2 note).
#pragma once

#include <QLoggingCategory>

namespace nuvio::mpv {
namespace logcat {

inline const QLoggingCategory& ctrl()   { static const QLoggingCategory c("nuvio.mpv.ctrl");   return c; }
inline const QLoggingCategory& render() { static const QLoggingCategory c("nuvio.mpv.render"); return c; }
inline const QLoggingCategory& keys()   { static const QLoggingCategory c("nuvio.mpv.keys");   return c; }
inline const QLoggingCategory& policy() { static const QLoggingCategory c("nuvio.mpv.policy"); return c; }
inline const QLoggingCategory& conf()   { static const QLoggingCategory c("nuvio.mpv.conf");   return c; }
inline const QLoggingCategory& mpvlog() { static const QLoggingCategory c("nuvio.mpv.log", QtInfoMsg); return c; }

} // namespace logcat
} // namespace nuvio::mpv

// Canonical greppable aliases used throughout systems/mpv.
#define lcNuvioMpvCtrl    ::nuvio::mpv::logcat::ctrl()
#define lcNuvioMpvRender  ::nuvio::mpv::logcat::render()
#define lcNuvioMpvKeys    ::nuvio::mpv::logcat::keys()
#define lcNuvioMpvPolicy  ::nuvio::mpv::logcat::policy()
#define lcNuvioMpvConf    ::nuvio::mpv::logcat::conf()
#define lcNuvioMpvLog     ::nuvio::mpv::logcat::mpvlog()

namespace nuvio::mpv {

/**
 * Logging policy, deterministic across distros whose default rules differ:
 *  - OUR namespaces are always enabled (info included) — harnesses, banners
 *    and diagnostics must be greppable on every machine (CI determinism).
 *  - NUVIO_MPV_DEBUG=1 additionally widens libmpv message mirroring.
 */
inline void applyMpvDebugEnv()
{
    const bool dbg = qEnvironmentVariableIntValue("NUVIO_MPV_DEBUG") == 1;
    QString rules = QStringLiteral("nuvio.*=true\nqt.qml.binding.removal.info=false\n");
    if (dbg)
        rules += QStringLiteral("nuvio.mpv.log.info=true\n");
    QLoggingCategory::setFilterRules(rules);
    if (dbg)
        qCInfo(lcNuvioMpvCtrl, "NUVIO_MPV_DEBUG=1 -> verbose libmpv mirroring on");
}

} // namespace nuvio::mpv
