// Built-systems truth table. Serves `--dump-modules`; statuses mirror plan
// phase mapping EXACTLY as implemented in this tree (accuracy doctrine).
#pragma once

#include <QStringList>

namespace nuvio::app {

struct ModuleInfo {
    QString name;
    QString status;      // "active" | "reserved (Pn)"
};

[[nodiscard]] inline const QVector<ModuleInfo>& modules()
{
    using M = ModuleInfo;
    static const QVector<M> kModules = {
        {QStringLiteral("platform"),     QStringLiteral("active")},
        {QStringLiteral("mpv"),          QStringLiteral("active (P0 core)")},
        {QStringLiteral("ui.shell"),     QStringLiteral("active (minimal)")},
        {QStringLiteral("settings"),     QStringLiteral("reserved (P1)")},
        {QStringLiteral("library"),      QStringLiteral("reserved (P1/P3)")},
        {QStringLiteral("playback"),     QStringLiteral("reserved (P2/P3)")},
        {QStringLiteral("integrations"), QStringLiteral("reserved (P3)")},
        {QStringLiteral("tracking"),     QStringLiteral("reserved (P3/P4)")},
        {QStringLiteral("authsync"),     QStringLiteral("reserved (P4, contract-tests-first)")},
    };
    return kModules;
}

} // namespace nuvio::app
