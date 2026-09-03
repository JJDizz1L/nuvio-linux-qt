#pragma once

// QML bridge over the pure NextEpisodeRules (P3a): maps/variants in,
// maps/bools out. "today" for hasAired is the device date.
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace nuvio::playback {

class NextEpisodeHelper final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    /// Next entry after (season,episode) in `videos` (each {season,episode,
    /// name,thumb?}); `parentId` prefixes the composite id. {} when none.
    Q_INVOKABLE QVariantMap nextEpisode(const QVariantList& videos,
                                        const QString& parentId, int season,
                                        int episode) const;
    /// Card rule; `intervals` are {startSec,endSec,type} (skip leg feeds
    /// these in P3c; empty in P3a falls back to the plain threshold).
    Q_INVOKABLE bool shouldShowCard(
        qint64 positionMs, qint64 durationMs, const QVariantList& intervals,
        const QString& thresholdMode, float thresholdPercent,
        float thresholdMinutesBeforeEnd) const;
    /// Unknown/unparseable dates count as aired (Compose parity).
    Q_INVOKABLE bool hasAired(const QString& released) const;
};

} // namespace nuvio::playback
