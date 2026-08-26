// Engine-level registration: types into the import namespace, image provider
// slot. Kept deliberately boring — wiring is visible from main.cpp.
#pragma once

#include <QtQml/QQmlEngine>

namespace nuvio::ui {

/** Idempotent per engine instance. */
void registerWith(QQmlEngine& engine);

} // namespace nuvio::ui
