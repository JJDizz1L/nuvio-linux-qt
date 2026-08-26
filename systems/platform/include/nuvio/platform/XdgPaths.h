// XDG Base Directory resolution (spec: https://specifications.freedesktop.org/basedir-spec/)
// All lookups are environment-driven with spec-mandated fallbacks.
// Nothing here is machine-specific; on any conforming system these yield the
// same directories the Compose line uses ($XDG_CONFIG_HOME/nuvio-linux, ...).
#pragma once

#include <QString>

namespace nuvio::platform {

// $XDG_CONFIG_HOME or $HOME/.config
[[nodiscard]] QString configHome();
// $XDG_STATE_HOME   or $HOME/.local/state
[[nodiscard]] QString stateHome();
// $XDG_CACHE_HOME   or $HOME/.cache
[[nodiscard]] QString cacheHome();
// $HOME (from env HOME; empty if unset — callers must handle)
[[nodiscard]] QString homeDir();

// Per-app roots following the established "nuvio-linux" convention:
// <configHome>/nuvio-linux, <stateHome>/nuvio-linux, <cacheHome>/nuvio-linux
[[nodiscard]] QString appConfigDir();
[[nodiscard]] QString appStateDir();
[[nodiscard]] QString appCacheDir();

} // namespace nuvio::platform
