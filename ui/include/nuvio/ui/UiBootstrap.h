// Engine-level registration: types into the import namespace, image provider
// slot. Kept deliberately boring — wiring is visible from main.cpp.
#pragma once

#include <QtQml/QQmlEngine>
#include <QString>

namespace nuvio::ui {

/** Idempotent per engine instance. */
void registerWith(QQmlEngine& engine);

/// Loads the bundled JetBrains Sans faces (Compose-parity typography,
/// embedded via qt_add_resources under :/nuvio/assets/fonts/) and installs
/// the family as the application font. Call AFTER QGuiApplication exists,
/// before any QML loads. Returns the applied family name; empty when no
/// face could be registered (caller reports through its stderr channel).
QString loadBundledFonts();

} // namespace nuvio::ui
