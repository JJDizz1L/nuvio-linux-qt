#include "nuvio/debrid/DebridFileSelect.h"

#include <algorithm>

namespace nuvio::debrid {

namespace {
const char* const kVideoExtensions[] = {".mp4", ".mkv", ".webm",
                                        ".avi", ".mov", ".m4v"};

bool hasVideoExtension(const QString& lower)
{
    for (const char* ext : kVideoExtensions) {
        if (lower.endsWith(QLatin1String(ext))) return true;
    }
    return false;
}

QString normalizedName(const QString& displayName)
{
    // Compose normalizedName: basename without extension, lowercased.
    QString base = displayName;
    const int slash = std::max(base.lastIndexOf(u'/'), base.lastIndexOf(u'\\'));
    if (slash >= 0) base = base.mid(slash + 1);
    const int dot = base.lastIndexOf(u'.');
    if (dot > 0) base = base.left(dot);
    return base.toLower();
}

bool nameMatches(const QString& fileName, const QString& wanted)
{
    // Compose firstNameMatch: normalized contains either way.
    const QString f = normalizedName(fileName);
    const QString w = normalizedName(wanted);
    if (f.isEmpty() || w.isEmpty()) return false;
    return f.contains(w) || w.contains(f);
}
} // namespace

bool isPlayableVideo(const QString& displayName, const QString& mimeType)
{
    if (!mimeType.isEmpty() &&
        mimeType.toLower().startsWith(QLatin1String("video/")))
        return true;
    // Empty mime (Real-Debrid) falls through to the extension check,
    // exactly like Compose's vendor split.
    return hasVideoExtension(displayName.toLower());
}

QStringList episodePatterns(int season, int episode)
{
    if (season < 0 || episode < 0) return {};
    const QString s = QString::number(season).rightJustified(2, u'0');
    const QString e = QString::number(episode).rightJustified(2, u'0');
    return {u's' + s + u'e' + e, QString::number(season) + u'x' + e,
            QString::number(season) + u'x' + QString::number(episode)};
}

std::optional<TorrentFile> selectTorrentFile(
    const QList<TorrentFile>& files, const QStringList& specificNames,
    int season, int episode, int fileIdx)
{
    QList<TorrentFile> playable;
    for (const TorrentFile& f : files) {
        if (isPlayableVideo(f.name, f.mimeType)) playable.append(f);
    }
    if (playable.isEmpty()) return std::nullopt;

    const QStringList patterns = episodePatterns(season, episode);
    if (!specificNames.isEmpty()) {
        for (const QString& wanted : specificNames) {
            for (const TorrentFile& f : playable) {
                if (nameMatches(f.name, wanted)) return f;
            }
        }
    }
    if (!patterns.isEmpty()) {
        for (const TorrentFile& f : playable) {
            const QString lower = f.name.toLower();
            for (const QString& p : patterns) {
                if (lower.contains(p)) return f;
            }
        }
    }
    if (fileIdx >= 0) {
        if (fileIdx < files.size() &&
            isPlayableVideo(files[fileIdx].name, files[fileIdx].mimeType))
            return files[fileIdx];
        if (fileIdx > 0 && fileIdx - 1 < files.size() &&
            isPlayableVideo(files[fileIdx - 1].name,
                            files[fileIdx - 1].mimeType))
            return files[fileIdx - 1];
        for (const TorrentFile& f : playable) {
            if (f.id == fileIdx) return f;
        }
    }
    const TorrentFile* biggest = nullptr;
    for (const TorrentFile& f : playable) {
        if (!biggest || f.size > biggest->size) biggest = &f;
    }
    if (!biggest) return std::nullopt;
    return *biggest;
}

} // namespace nuvio::debrid
