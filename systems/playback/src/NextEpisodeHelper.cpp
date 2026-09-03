#include "nuvio/playback/NextEpisodeHelper.h"

#include <QDate>

#include "nuvio/playback/NextEpisodeRules.h"

namespace nuvio::playback {

QVariantMap NextEpisodeHelper::nextEpisode(const QVariantList& videos,
                                          const QString& parentId, int season,
                                          int episode) const
{
    QList<EpisodeRef> refs;
    for (const QVariant& v : videos) {
        const QVariantMap m = v.toMap();
        EpisodeRef r;
        r.season = m.value(QStringLiteral("season"), -1).toInt();
        r.episode = m.value(QStringLiteral("episode"), -1).toInt();
        r.name = m.value(QStringLiteral("name")).toString();
        refs.append(r);
    }
    const auto next = resolveNextEpisode(refs, season, episode);
    if (!next) return {};
    return QVariantMap{
        {QStringLiteral("id"),
         parentId + u':' + QString::number(next->season) + u':' +
             QString::number(next->episode)},
        {QStringLiteral("season"), next->season},
        {QStringLiteral("episode"), next->episode},
        {QStringLiteral("name"), next->name},
    };
}

bool NextEpisodeHelper::shouldShowCard(
    qint64 positionMs, qint64 durationMs, const QVariantList& intervals,
    const QString& thresholdMode, float thresholdPercent,
    float thresholdMinutesBeforeEnd) const
{
    QList<SkipSegment> segs;
    for (const QVariant& v : intervals) {
        const QVariantMap m = v.toMap();
        SkipSegment s;
        s.startSec = m.value(QStringLiteral("startSec")).toDouble();
        s.endSec = m.value(QStringLiteral("endSec")).toDouble();
        s.type = m.value(QStringLiteral("type")).toString();
        segs.append(s);
    }
    return shouldShowNextEpisodeCard(positionMs, durationMs, segs,
                                     thresholdMode, thresholdPercent,
                                     thresholdMinutesBeforeEnd);
}

bool NextEpisodeHelper::hasAired(const QString& released) const
{
    const QDate today = QDate::currentDate();
    return ::nuvio::playback::hasAired(released, today.year(), today.month(),
                                       today.day());
}

} // namespace nuvio::playback
