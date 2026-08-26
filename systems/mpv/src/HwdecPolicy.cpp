#include "nuvio/mpv/HwdecPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace nuvio::mpv {

namespace {
constexpr const char* kNvidiaModuleSysfs = "/sys/module/nvidia";
constexpr const char* kNvidiaCharDev     = "/dev/nvidiactl";

bool containsAny(const QString& hay, std::initializer_list<const char*> needles)
{
    for (const auto* n : needles)
        if (hay.contains(QLatin1String(n))) return true;
    return false;
}
} // namespace

QString HwdecPolicy::userHwdecOverride()
{
    return QString::fromLocal8Bit(qgetenv("NUVIO_MPV_HWDEC")).trimmed();
}

bool HwdecPolicy::nvidiaDetectedBySystem()
{
    static const bool cached = [] {
        return QFile::exists(QLatin1String(kNvidiaModuleSysfs))
            || QFile::exists(QLatin1String(kNvidiaCharDev));
    }();
    return cached;
}

QString HwdecPolicy::selectChain(const QString& glVendorLower, bool nvidiaViaFiles)
{
    const QString override_ = userHwdecOverride();
    if (!override_.isEmpty()) return override_;

    const bool nvidiaSession =
        nvidiaViaFiles || glVendorLower.contains(QLatin1String("nvidia"));

    if (nvidiaSession)                    return QStringLiteral("nvdec,nvdec-copy");
    if (containsAny(glVendorLower, {"amd", "amdgpu", "advanced micro",
                                    "mesa", "intel"}))
                                          return QStringLiteral("vaapi,vaapi-copy");
    return QStringLiteral("auto-copy");
}

QStringList HwdecPolicy::enumerateDrmRenderNodes()
{
    QDir dri(QStringLiteral("/dev/dri"));
    QStringList names =
        dri.entryList(QStringList{QLatin1String("renderD*")},
                      QDir::System | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    QStringList out;
    out.reserve(names.size());
    for (const QString& n : names)
        out << dri.absoluteFilePath(n);
    return out;
}

QString HwdecPolicy::pickDrmNode(const QString& glVendorLower)
{
    const QStringList nodes = enumerateDrmRenderNodes();
    if (nodes.isEmpty()) return {};

    // /sys/class/drm/<basename>/device/driver -> kernel driver basename.
    auto driverOf = [](const QString& node) -> QString {
        const QString base = QFileInfo(node).fileName();
        const QFileInfo fi(
            QStringLiteral("/sys/class/drm/") + base +
            QStringLiteral("/device/driver"));
        return fi.exists() ? fi.fileName() : QString();
    };

    const bool amdGl   = containsAny(glVendorLower, {"amd", "amdgpu"});
    const bool intelGl = containsAny(glVendorLower, {"intel"});

    // Tier 2 heuristic: node bound to the GL device's kernel driver.
    for (const QString& node : nodes) {
        const QString drv = driverOf(node);
        if (drv.isEmpty()) continue;
        if (amdGl && drv == QLatin1String("amdgpu"))          return node;
        if (intelGl && (drv == QLatin1String("i915")
                     || drv == QLatin1String("xe")))           return node;
    }

    return nodes.first(); // legacy lottery fallback
}

} // namespace nuvio::mpv
