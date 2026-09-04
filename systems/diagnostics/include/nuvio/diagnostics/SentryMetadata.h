#pragma once

// Sentry release metadata (Appendix A, Sentry): verbatim port of Compose's
// DesktopSentryMetadata normalization (platform/arch buckets, release +
// distribution shapes). Package id is this product line's desktop file id;
// the version code derives deterministically from the dotted version
// (major*1e6 + minor*1e4 + patch*1e2 + tweak).

#include <QString>

namespace nuvio::diagnostics {

inline constexpr char kSentryPackage[] = "io.github.jjdizz1l.NuvioLinux";

[[nodiscard]] QString normalizeSentryPlatform(const QString& osName);
[[nodiscard]] QString normalizeSentryArchitecture(const QString& arch);
[[nodiscard]] int versionCodeFromString(const QString& dotted);
[[nodiscard]] QString sentryRelease(const QString& package,
                                   const QString& versionName, int versionCode);
[[nodiscard]] QString sentryDistribution(int versionCode,
                                        const QString& platform,
                                        const QString& architecture);

struct SentryHostMetadata {
    QString platform;
    QString architecture;
    QString release;
    QString distribution;
};
/// Live metadata for this machine (kernelType/currentCpuArchitecture feed
/// the same buckets the fork feeds from os.name/os.arch).
[[nodiscard]] SentryHostMetadata currentSentryMetadata(
    const QString& versionName);

} // namespace nuvio::diagnostics
