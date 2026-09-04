#pragma once

// GitHub release -> AppUpdate parsing (Appendix A, updater): verbatim port
// of Compose's AppUpdaterRepository.getLatestChannelUpdate selection
// (channel match, skip drafts, prerelease gate, tag falls back to name,
// best-asset pick or nothing). Release source points at THIS product line
// (JJDizz1L/nuvio-linux-qt) - the fork's NuvioMedia/NuvioDesktop source
// would offer the Compose desktop build to Qt users (deliberate product-
// identity divergence; with no Qt releases published yet, checks honestly
// report "latest").

#include <QByteArray>
#include <QString>
#include <optional>

namespace nuvio::updater {

inline constexpr char kUpdateOwner[] = "JJDizz1L";
inline constexpr char kUpdateRepo[] = "nuvio-linux-qt";
inline constexpr char kUpdateUserAgent[] = "NuvioLinuxQt";
inline constexpr char kGitHubApiBase[] = "https://api.github.com";

struct AppUpdate {
    QString tag;
    QString title;
    QString notes;
    QString releaseUrl;
    QString assetName;
    QString assetUrl;
    long long assetSizeBytes = -1;   // -1 = unknown (fork null)
};

/// Parses a GitHub releases-list body. No matching release (or a
/// non-array body) yields an empty update WITHOUT malformed set
/// (NoChannelReleaseException parity: silent unless a forced check with
/// feedback). A matched release with no tag/name or no installable asset
/// sets malformed (real error: forced checks surface it).
/// channelBranch empty = every release matches (desktop parity).
struct LatestUpdateResult {
    std::optional<AppUpdate> update;
    bool malformed = false;
};
[[nodiscard]] LatestUpdateResult parseLatestUpdate(
    const QByteArray& releasesJson, bool includePrereleases,
    const QString& channelBranch = {});

} // namespace nuvio::updater
