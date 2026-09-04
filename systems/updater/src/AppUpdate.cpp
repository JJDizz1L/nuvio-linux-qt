#include "nuvio/updater/AppUpdate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "nuvio/updater/UpdateAssets.h"

namespace nuvio::updater {

namespace {

bool matchesChannel(const QJsonObject& release, const QString& channel)
{
    if (channel.trimmed().isEmpty()) return true;
    const QString target =
        release.value(QStringLiteral("target_commitish")).toString().trimmed();
    if (!target.isEmpty() && target.compare(channel.trimmed(),
                                            Qt::CaseInsensitive) == 0)
        return true;
    for (const char* field : {"tag_name", "name"}) {
        const QString value =
            release.value(QLatin1String(field)).toString();
        if (!value.isEmpty() &&
            value.contains(channel.trimmed(), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

} // namespace

LatestUpdateResult parseLatestUpdate(const QByteArray& releasesJson,
                                       bool includePrereleases,
                                       const QString& channelBranch)
{
    LatestUpdateResult out;
    const QJsonDocument doc = QJsonDocument::fromJson(releasesJson);
    if (!doc.isArray()) return out;
    const UpdateAssetSelector selector = linuxAssetSelector();
    for (const QJsonValue& rv : doc.array()) {
        const QJsonObject release = rv.toObject();
        if (release.isEmpty() && !rv.isObject()) continue;
        if (!matchesChannel(release, channelBranch)) continue;
        if (release.value(QStringLiteral("draft")).toBool(false)) continue;
        if (!includePrereleases &&
            release.value(QStringLiteral("prerelease")).toBool(false))
            continue;
        QString tag = release.value(QStringLiteral("tag_name")).toString();
        if (tag.trimmed().isEmpty())
            tag = release.value(QStringLiteral("name")).toString();
        if (tag.trimmed().isEmpty()) {
            out.malformed = true;
            return out;
        }
        QList<UpdateAssetCandidate> candidates;
        for (const QJsonValue& av : release.value(QStringLiteral("assets"))
                                        .toArray()) {
            const QJsonObject asset = av.toObject();
            UpdateAssetCandidate candidate;
            candidate.name = asset.value(QStringLiteral("name")).toString();
            candidate.downloadUrl =
                asset.value(QStringLiteral("browser_download_url"))
                    .toString();
            candidate.size = asset.value(QStringLiteral("size"))
                                 .toInteger(-1);
            candidate.contentType =
                asset.value(QStringLiteral("content_type")).toString();
            if (candidate.name.isEmpty() ||
                candidate.downloadUrl.isEmpty())
                continue;
            candidates.append(candidate);
        }
        const UpdateAssetCandidate* best =
            selectBestUpdateAsset(candidates, selector);
        if (!best) {
            out.malformed = true;
            return out;
        }
        AppUpdate update;
        update.tag = tag;
        QString title = release.value(QStringLiteral("name")).toString();
        update.title = title.trimmed().isEmpty() ? tag : title;
        update.notes = release.value(QStringLiteral("body")).toString();
        update.releaseUrl =
            release.value(QStringLiteral("html_url")).toString();
        update.assetName = best->name;
        update.assetUrl = best->downloadUrl;
        update.assetSizeBytes = best->size;
        out.update = update;
        return out;
    }
    return out;
}

} // namespace nuvio::updater
