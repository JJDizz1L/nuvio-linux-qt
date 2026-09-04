#include "nuvio/diagnostics/SentryMetadata.h"

#include <QStringList>
#include <QSysInfo>

namespace nuvio::diagnostics {

QString normalizeSentryPlatform(const QString& osName)
{
    const QString v = osName.toLower();
    if (v.contains("mac")) return QStringLiteral("macos");
    if (v.contains("win")) return QStringLiteral("windows");
    if (v.contains("linux")) return QStringLiteral("linux");
    return QStringLiteral("other");
}

QString normalizeSentryArchitecture(const QString& arch)
{
    const QString v = arch.toLower();
    if (v == "aarch64" || v == "arm64") return QStringLiteral("arm64");
    if (v == "amd64" || v == "x64" || v == "x86_64")
        return QStringLiteral("x86_64");
    if (v == "x86" || v == "i386" || v == "i686")
        return QStringLiteral("x86");
    return v.isEmpty() ? QStringLiteral("unknown") : v;
}

int versionCodeFromString(const QString& dotted)
{
    const QStringList parts = dotted.split(u'.');
    auto at = [&](int i) {
        bool ok = false;
        const int v = parts.value(i).toInt(&ok);
        return ok ? v : 0;
    };
    return at(0) * 1000000 + at(1) * 10000 + at(2) * 100 + at(3);
}

QString sentryRelease(const QString& package, const QString& versionName,
                      int versionCode)
{
    return package + u'@' + versionName + u'+' +
           QString::number(versionCode);
}

QString sentryDistribution(int versionCode, const QString& platform,
                           const QString& architecture)
{
    return QString::number(versionCode) + u'-' + platform + u'-' +
           architecture;
}

SentryHostMetadata currentSentryMetadata(const QString& versionName)
{
    SentryHostMetadata out;
    out.platform = normalizeSentryPlatform(QSysInfo::kernelType());
    out.architecture =
        normalizeSentryArchitecture(QSysInfo::currentCpuArchitecture());
    const int code = versionCodeFromString(versionName);
    out.release = sentryRelease(QString::fromLatin1(kSentryPackage),
                                versionName, code);
    out.distribution =
        sentryDistribution(code, out.platform, out.architecture);
    return out;
}

} // namespace nuvio::diagnostics
