#pragma once

// Trakt scrobbler (T2): verbatim port of Compose TraktScrobbleRepository's
// wire contract (POST api.trakt.tv/scrobble/{start,pause,stop}, movie +
// episode bodies with omitted nulls, 8 s same-window throttle with a
// ±1.5 progress window, stop-after-start exemption, stop retried 2x) over
// TraktAuth's bearer. Registers itself into a TrackingRegistry as a
// STOP_AND_RESTART scrobbler when constructed.

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "nuvio/tracking/TrackingRegistry.h"

namespace nuvio::tracking {

class TraktAuth;

/// Scrobble request body (nulls omitted, Compose encodeDefaults=false +
/// explicitNulls=false parity).
[[nodiscard]] QJsonObject traktScrobbleBody(const TrackingMedia& media,
                                            double progressPercent,
                                            const QString& appVersion);

class TraktScrobbler final : public QObject {
    Q_OBJECT

public:
    explicit TraktScrobbler(TraktAuth* auth, TrackingRegistry* registry,
                            const QString& appVersion,
                            QObject* parent = nullptr);

    /// Direct entry for tests wirings (same path the registry uses).
    [[nodiscard]] bool scrobble(int profileId, ScrobbleAction action,
                                const ScrobbleEvent& event);

private:
    void send(const QString& action, const TrackingMedia& media,
              double progress, int attempt);

    TraktAuth* m_auth = nullptr;
    QString m_appVersion;
    // Throttle stamp (Compose ScrobbleStamp parity).
    int m_lastProfile = -1;
    QString m_lastAction;
    QString m_lastItemKey;
    double m_lastProgress = -1e9;
    qint64 m_lastTimestampMs = 0;
};

} // namespace nuvio::tracking
