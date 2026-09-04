#include "nuvio/updater/UpdateAssets.h"

#include <QSysInfo>

namespace nuvio::updater {

UpdateAssetSelector linuxAssetSelector(const QString& archOverride)
{
    const QString arch =
        (archOverride.isEmpty() ? QSysInfo::currentCpuArchitecture()
                                : archOverride)
            .toLower();
    QStringList archFragments;
    if (arch == "aarch64" || arch == "arm64")
        archFragments = {"arm64", "aarch64"};
    else if (arch == "x86" || arch == "i386" || arch == "i686")
        archFragments = {"x86", "i386", "i686"};
    else if (arch.contains("64"))
        archFragments = {"x64", "x86_64", "amd64"};
    return UpdateAssetSelector{
        {".deb", ".AppImage"},
        {},
        archFragments + QStringList{"linux"},
        {"universal", "all"},
    };
}

bool assetMatches(const UpdateAssetCandidate& asset,
                  const UpdateAssetSelector& selector)
{
    for (const QString& ext : selector.fileExtensions) {
        if (asset.name.endsWith(ext, Qt::CaseInsensitive)) return true;
    }
    for (const QString& type : selector.contentTypes) {
        if (!type.isEmpty() &&
            type.compare(asset.contentType, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

const UpdateAssetCandidate* selectBestUpdateAsset(
    const QList<UpdateAssetCandidate>& assets,
    const UpdateAssetSelector& selector)
{
    QList<const UpdateAssetCandidate*> matched;
    for (const UpdateAssetCandidate& asset : assets) {
        if (assetMatches(asset, selector)) matched.append(&asset);
    }
    if (matched.isEmpty()) return nullptr;
    if (matched.size() == 1) return matched.first();
    for (const QString& fragment : selector.preferredNameFragments) {
        for (const UpdateAssetCandidate* candidate : matched) {
            if (candidate->name.contains(fragment, Qt::CaseInsensitive))
                return candidate;
        }
    }
    for (const UpdateAssetCandidate* candidate : matched) {
        const QString name = candidate->name.toLower();
        for (const QString& fragment : selector.fallbackNameFragments) {
            if (name.contains(fragment.toLower())) return candidate;
        }
    }
    return matched.first();
}

} // namespace nuvio::updater
