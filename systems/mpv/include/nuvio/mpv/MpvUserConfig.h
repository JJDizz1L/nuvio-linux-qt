// User mpv configuration discovery + deliberate input-conf pointing.
//
// Doctrine (plan §"keyboard ownership"): libmpv NEVER auto-discovers
// input.conf — a documented render-API limitation. We therefore point the
// `input-conf` option at the file explicitly whenever one exists beside the
// user's mpv.conf. When NO user config exists anywhere, defaults stay:
// input-default-bindings=yes alone, and the app synthesizes nothing.
#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

#include <functional>

struct mpv_handle;

namespace nuvio::mpv {

struct UserConfig {
    bool    mpvConfFound  = false;
    QString mpvConfPath;
    QString inputConfPath;      ///< non-empty only when input.conf exists alongside
};

class MpvUserConfig {
public:
    /// Environment probes injectable for tests; production wraps qgetenv.
    using EnvView        = QMap<QByteArray, QByteArray>;
    using FileExistsFunc = std::function<bool(const QString&)>;

    /// Ordered candidates exactly like the Compose bridge walked them:
    ///   1. $MPV_HOME/mpv.conf
    ///   2. $XDG_CONFIG_HOME (default ~/.config) /mpv/mpv.conf
    ///   3. ~/.mpv/mpv.conf
    [[nodiscard]] static UserConfig discover(const EnvView& env,
                                             const FileExistsFunc& exists);

    [[nodiscard]] static UserConfig discover();   ///< real-environment wrapper

    /**
     * Pre-initialize application:
     *  - loads user mpv.conf wholesale when found,
     *  - sets input-default-bindings=yes always,
     *  - points input-conf at the user's file when present.
     * @param errOut receives human-readable failure reason on false.
     * @return success (safe to proceed to mpv_initialize).
     */
    static bool apply(mpv_handle* h, const UserConfig& cfg, QString* errOut);
};

} // namespace nuvio::mpv
