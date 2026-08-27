#pragma once

// Screen-stack navigation viewmodel (plan §7.1 ui layer: no QtQuick
// includes here — QML binds through properties/invokables only).
//
// Contract:
//  * Stack NEVER empties: depth >= 1, seeded with a root route at birth.
//    pop()/popToRoot() clamp instead of underflowing.
//  * Pushing the route already on top is a no-op (double-click guard);
//    pushing any other route duplicates are allowed (A,B,A legal).
//  * stateChanged fires only on actual mutation; identical transitions
//    (replace with same route) stay silent to spare QML re-layouts.
//
// Unit-tested headless in ui/tests — keep this class free of anything that
// would force a GUI session to run those tests.

#include <QObject>
#include <QString>
#include <QStringList>

namespace nuvio::ui {

class NavigationModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentRoute READ currentRoute NOTIFY stateChanged)
    Q_PROPERTY(int depth READ depth NOTIFY stateChanged)

public:
    explicit NavigationModel(QObject* parent = nullptr);

    /// Depth is clamped: never below the root entry.
    Q_INVOKABLE void push(const QString& route);

    /// Consecutive-duplicate guard: pushing the same route twice in a row
    /// is ignored (buttons love double clicks).
    Q_INVOKABLE bool pushIfDifferent(const QString& route);

    /// No-op when already at the root.
    Q_INVOKABLE void pop();

    /// Collapse to the seeded root, keeping its route name.
    Q_INVOKABLE void popToRoot();

    /// Swap the top entry (used by deep-link launches); no-op when empty.
    Q_INVOKABLE void replaceTop(const QString& route);

    [[nodiscard]] QString currentRoute() const;
    [[nodiscard]] int     depth() const;
    [[nodiscard]] bool    canPop() const;

signals:
    void stateChanged();

private:
    QStringList m_stack{QStringLiteral("home")};
};

} // namespace nuvio::ui