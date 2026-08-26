#include "nuvio/platform/XdgPaths.h"

#include <QDir>
#include <QFileInfo>

namespace nuvio::platform {

namespace {
[[nodiscard]] QString envOr(const char* name, const QString& fallbackPath)
{
    const QByteArray v = qgetenv(name);
    if (!v.isEmpty()) {
        const QString s = QString::fromLocal8Bit(v);
        // Spec: non-absolute paths must be ignored.
        if (QDir::isAbsolutePath(s)) return s;
    }
    return fallbackPath;
}
} // namespace

QString homeDir() { return QString::fromLocal8Bit(qgetenv("HOME")); }

QString configHome() { return envOr("XDG_CONFIG_HOME", homeDir() + "/.config"); }
QString stateHome()   { return envOr("XDG_STATE_HOME",   homeDir() + "/.local/state"); }
QString cacheHome()   { return envOr("XDG_CACHE_HOME",   homeDir() + "/.cache"); }

QString appConfigDir() { return configHome() + "/nuvio-linux"; }
QString appStateDir()  { return stateHome()  + "/nuvio-linux"; }
QString appCacheDir()  { return cacheHome()  + "/nuvio-linux"; }

} // namespace nuvio::platform
