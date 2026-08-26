#include "nuvio/mpv/MpvUserConfig.h"

#include <QFile>

#include <mpv/client.h>

namespace nuvio::mpv {

namespace {
QString envOf(const MpvUserConfig::EnvView& env, const char* key)
{
    return QString::fromLocal8Bit(env.value(QByteArray(key)));
}

// First absolute value wins (XDG spec); relative values ignored.
QString envDir(const MpvUserConfig::EnvView& env, const char* key,
               const QString& fallback)
{
    const QString v = envOf(env, key);
    if (!v.isEmpty() && v.startsWith(u'/')) return v;
    return fallback;
}
} // namespace

UserConfig MpvUserConfig::discover(const EnvView& env,
                                   const FileExistsFunc& exists)
{
    const QString home = envOf(env, "HOME");

    QString mpvConf;
    do {
        const QString mpvHome = envOf(env, "MPV_HOME");
        if (!mpvHome.isEmpty()) {
            const QString c = mpvHome + QStringLiteral("/mpv.conf");
            if (exists(c)) { mpvConf = c; break; }
        }
        const QString xdgBase =
            envDir(env, "XDG_CONFIG_HOME", home + QStringLiteral("/.config"));
        const QString c = xdgBase + QStringLiteral("/mpv/mpv.conf");
        if (exists(c)) { mpvConf = c; break; }

        const QString legacy = home + QStringLiteral("/.mpv/mpv.conf");
        if (!home.isEmpty() && exists(legacy)) { mpvConf = legacy; break; }
    } while (false);

    UserConfig out;
    if (mpvConf.isEmpty()) return out;

    out.mpvConfFound = true;
    out.mpvConfPath  = mpvConf;

    // input.conf rides in the same directory as the winning mpv.conf.
    const QString sibling =
        mpvConf.section(u'/', 0, -2) + QStringLiteral("/input.conf");
    if (exists(sibling)) out.inputConfPath = sibling;
    return out;
}

UserConfig MpvUserConfig::discover()
{
    EnvView env;
    for (const char* k : {"HOME", "MPV_HOME", "XDG_CONFIG_HOME"}) {
        const QByteArray v = qgetenv(k);
        if (!v.isEmpty()) env.insert(QByteArray(k), v);
    }
    return discover(env, [](const QString& p) { return QFile::exists(p); });
}

bool MpvUserConfig::apply(mpv_handle* h, const UserConfig& cfg, QString* errOut)
{
    auto fail = [errOut](const QString& m) {
        if (errOut) *errOut = m;
        return false;
    };

    int rc = 0;
    rc = mpv_set_option_string(h, "input-default-bindings", "yes");
    if (rc < 0) return fail(QStringLiteral("set input-default-bindings: %1").arg(rc));

    if (cfg.mpvConfFound) {
        const QByteArray u8 = QFile::encodeName(cfg.mpvConfPath);
        rc = mpv_load_config_file(h, u8.constData());
        if (rc < 0) return fail(QStringLiteral("load %1: %2").arg(cfg.mpvConfPath).arg(rc));
    }

    if (!cfg.inputConfPath.isEmpty()) {
        const QByteArray u8 = QFile::encodeName(cfg.inputConfPath);
        rc = mpv_set_option_string(h, "input-conf", u8.constData());
        if (rc < 0) return fail(QStringLiteral("point input-conf at %1: %2").arg(cfg.inputConfPath).arg(rc));
    }
    return true;
}

} // namespace nuvio::mpv
