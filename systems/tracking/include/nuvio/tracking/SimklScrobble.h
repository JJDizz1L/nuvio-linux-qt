#pragma once

// SIMKL scrobbler (T3): direct POST api.simkl.com/scrobble/{start,pause,
// stop} with {progress, movie|show|anime, episode} (verbatim DTO shapes:
// ANIME with a season rides the show leg, else the anime leg; progress
// rounded to 2 dp; ids via the verbatim id table). This is the direct
// scrobble path; the full sync-engine reconciliation (snapshots,
// projections, playback merge) stays in the backlog with the engine.

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "nuvio/tracking/TrackingRegistry.h"

namespace nuvio::tracking {

class SimklAuth;

/// Scrobble request body (nulls omitted).
[[nodiscard]] QJsonObject simklScrobbleBody(const TrackingMedia& media,
                                            double progressPercent);

class SimklScrobbler final : public QObject {
    Q_OBJECT

public:
    explicit SimklScrobbler(SimklAuth* auth, TrackingRegistry* registry,
                            QObject* parent = nullptr);

    [[nodiscard]] bool scrobble(int profileId, ScrobbleAction action,
                                const ScrobbleEvent& event);

private:
    SimklAuth* m_auth = nullptr;
};

} // namespace nuvio::tracking
