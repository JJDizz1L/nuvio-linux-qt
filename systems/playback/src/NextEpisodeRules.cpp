#include "nuvio/playback/NextEpisodeRules.h"

#include <QRegularExpression>

#include <algorithm>

namespace nuvio::playback {

namespace {
bool episodeLess(const EpisodeRef& a, const EpisodeRef& b)
{
    if (a.season != b.season) return a.season < b.season;
    return a.episode < b.episode;
}

[[nodiscard]] bool isOutroType(const QString& type)
{
    const QString t = type.toLower();
    return t == QLatin1String("outro") || t == QLatin1String("ed") ||
           t == QLatin1String("mixed-ed");
}
} // namespace

std::optional<EpisodeRef> resolveNextEpisode(const QList<EpisodeRef>& videos,
                                             int curSeason, int curEpisode)
{
    if (curSeason < 0 || curEpisode < 0) return std::nullopt;
    QList<EpisodeRef> sorted;
    for (const auto& v : videos) {
        if (v.season < 0 || v.episode < 0) continue;
        sorted.append(v);
    }
    std::sort(sorted.begin(), sorted.end(), episodeLess);
    for (int i = 0; i < sorted.size(); ++i) {
        if (sorted[i].season == curSeason && sorted[i].episode == curEpisode)
            return i + 1 < sorted.size() ? std::optional<EpisodeRef>(sorted[i + 1])
                                         : std::nullopt;
    }
    return std::nullopt;
}

bool shouldShowNextEpisodeCard(qint64 positionMs, qint64 durationMs,
                               const QList<SkipSegment>& intervals,
                               const QString& thresholdMode,
                               float thresholdPercent,
                               float thresholdMinutesBeforeEnd)
{
    const float pct =
        std::clamp(thresholdPercent, 97.0f, 100.0f);
    const float mins =
        std::clamp(thresholdMinutesBeforeEnd, 0.0f, 3.5f);
    const bool byMinutes = thresholdMode == QLatin1String("MINUTES_BEFORE_END");

    QList<SkipSegment> outros;
    for (const auto& s : intervals) {
        if (isOutroType(s.type)) outros.append(s);
    }
    if (!outros.isEmpty()) {
        if (durationMs <= 0) return false;
        double latestOutroEnd = 0.0;
        double earliestOutroStart = outros.first().startSec;
        for (const auto& o : outros) {
            latestOutroEnd = std::max(latestOutroEnd, o.endSec);
            earliestOutroStart = std::min(earliestOutroStart, o.startSec);
        }
        const qint64 latestOutroEndMs =
            static_cast<qint64>(latestOutroEnd * 1000.0);
        const qint64 postOutroGapMs = durationMs - latestOutroEndMs;
        const qint64 userThresholdMs =
            byMinutes ? static_cast<qint64>(mins * 60000.0f)
                      : static_cast<qint64>((1.0 - pct / 100.0) * durationMs);
        if (postOutroGapMs > userThresholdMs) {
            if (byMinutes)
                return durationMs - positionMs <=
                       static_cast<qint64>(mins * 60000.0f);
            return durationMs > 0 &&
                   (double(positionMs) / double(durationMs)) >= (pct / 100.0);
        }
        return double(positionMs) / 1000.0 >= earliestOutroStart;
    }

    if (durationMs <= 0) return false;
    if (byMinutes)
        return durationMs - positionMs <=
               static_cast<qint64>(mins * 60000.0f);
    return (double(positionMs) / double(durationMs)) >= (pct / 100.0);
}

bool hasAired(const QString& released, int todayYear, int todayMonth,
              int todayDay)
{
    const QString v = released.trimmed();
    if (v.isEmpty()) return true;
    if (v.size() < 10) return true;
    const QString date = v.left(10);
    const QStringList parts = date.split(u'-');
    if (parts.size() != 3) return true;
    bool okY = false, okM = false, okD = false;
    const int y = parts[0].toInt(&okY);
    const int m = parts[1].toInt(&okM);
    const int d = parts[2].toInt(&okD);
    if (!okY || !okM || !okD) return true;
    if (y != todayYear) return y < todayYear;
    if (m != todayMonth) return m < todayMonth;
    return d <= todayDay;
}

std::optional<QString> extractImdbId(const QString& value)
{
    static const QRegularExpression re(QStringLiteral("tt\\d+"));
    const auto m = re.match(value);
    if (!m.hasMatch()) return std::nullopt;
    const QString id = m.captured(0);
    return id.startsWith(QLatin1String("tt")) ? std::optional<QString>(id)
                                              : std::nullopt;
}

CompositeId splitCompositeId(const QString& id)
{
    CompositeId out;
    const QStringList parts = id.split(u':');
    if (parts.size() != 3) {
        out.parent = id;
        return out;
    }
    bool okS = false, okE = false;
    const int s = parts[1].toInt(&okS);
    const int e = parts[2].toInt(&okE);
    if (!okS || !okE || !parts[0].startsWith(QLatin1String("tt"))) {
        out.parent = id;
        return out;
    }
    out.parent = parts[0];
    out.season = s;
    out.episode = e;
    return out;
}

} // namespace nuvio::playback
