#pragma once

// Update asset selection (Appendix A, updater): verbatim port of Compose's
// AppUpdateAssetSelector / AppUpdateAssetCandidate / selectBestUpdateAsset
// plus the desktop Linux selector (extensions .deb/.AppImage, arch
// fragments + "linux" preferred, "universal"/"all" fallback).

#include <QList>
#include <QString>
#include <QStringList>

namespace nuvio::updater {

struct UpdateAssetSelector {
    QStringList fileExtensions;
    QStringList contentTypes;
    QStringList preferredNameFragments;
    QStringList fallbackNameFragments;
};

struct UpdateAssetCandidate {
    QString name;
    QString downloadUrl;
    long long size = -1;   // -1 = unknown (fork null)
    QString contentType;
};

/// This machine's selector (desktop Linux parity). archOverride feeds the
/// arch-fragment table in tests (fork reads os.arch; e.g. "amd64").
[[nodiscard]] UpdateAssetSelector linuxAssetSelector(
    const QString& archOverride = {});

[[nodiscard]] bool assetMatches(const UpdateAssetCandidate& asset,
                               const UpdateAssetSelector& selector);

/// Preferred-fragment order wins, then any fallback-fragment hit, then the
/// first match (selectBestUpdateAsset parity). Null when nothing matches.
[[nodiscard]] const UpdateAssetCandidate* selectBestUpdateAsset(
    const QList<UpdateAssetCandidate>& assets,
    const UpdateAssetSelector& selector);

} // namespace nuvio::updater
