#include "nuvio/tracking/TraktScrobble.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QTimer>

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/tracking/TraktAuth.h"

namespace nuvio::tracking {

namespace {
Q_LOGGING_CATEGORY(lcTrakt, "nuvio.tracking.trakt", QtWarningMsg)
constexpr auto kBase = "https://api.trakt.tv";
constexpr qint64 kMinIntervalMs = 8000;
constexpr double kProgressWindow = 1.5;
constexpr int kMaxStopAttempts = 3;   // 1 + 2 retries (Compose parity)

[[nodiscard]] QString itemKey(const TrackingMedia& media)
{
    // Episode coordinate present -> episode form, else the bare imdb id.
    if (media.episode.number >= 0 && !media.ids.imdb.isEmpty())
        return media.ids.imdb + u':' + QString::number(media.episode.season) +
               u':' + QString::number(media.episode.number);
    return media.ids.imdb;
}

void putIds(QJsonObject& o, const TrackingExternalIds& ids)
{
    // Trakt accepts imdb natively (our primary identity); other ids ride
    // along when known. Nulls omitted (explicitNulls=false parity).
    if (!ids.imdb.isEmpty()) o.insert(QStringLiteral("imdb"), ids.imdb);
    if (ids.tmdb >= 0) o.insert(QStringLiteral("tmdb"), ids.tmdb);
    if (!ids.tvdb.isEmpty()) o.insert(QStringLiteral("tvdb"), ids.tvdb);
    if (ids.trakt >= 0) o.insert(QStringLiteral("trakt"), ids.trakt);
}
} // namespace

QJsonObject traktScrobbleBody(const TrackingMedia& media,
                              double progressPercent, const QString& appVersion)
{
    const double progress =
        std::clamp(progressPercent, 0.0, 100.0);
    QJsonObject body;
    // Episode coordinate present -> episode leg, else movie (Compose
    // TraktEpisodeMappingInput parity in spirit: series always resolve).
    if (media.episode.number >= 0) {
        QJsonObject show{{QStringLiteral("title"), media.title}};
        QJsonObject showIds;
        putIds(showIds, media.ids);
        if (!showIds.isEmpty()) show.insert(QStringLiteral("ids"), showIds);
        body.insert(QStringLiteral("show"), show);
        QJsonObject episode{
            {QStringLiteral("season"), media.episode.season},
            {QStringLiteral("number"), media.episode.number},
        };
        if (!media.episode.title.isEmpty())
            episode.insert(QStringLiteral("title"), media.episode.title);
        body.insert(QStringLiteral("episode"), episode);
    } else {
        QJsonObject movie{{QStringLiteral("title"), media.title}};
        QJsonObject ids;
        putIds(ids, media.ids);
        if (!ids.isEmpty()) movie.insert(QStringLiteral("ids"), ids);
        body.insert(QStringLiteral("movie"), movie);
    }
    body.insert(QStringLiteral("progress"), progress);
    if (!appVersion.isEmpty())
        body.insert(QStringLiteral("app_version"), appVersion);
    return body;
}

TraktScrobbler::TraktScrobbler(TraktAuth* auth, TrackingRegistry* registry,
                               const QString& appVersion, QObject* parent)
    : QObject(parent), m_auth(auth), m_appVersion(appVersion)
{
    if (registry) {
        registry->registerScrobbler(
            TrackingProvider::Trakt, SeekScrobblePolicy::StopAndRestart,
            [this](int profileId, ScrobbleAction action,
                   const ScrobbleEvent& event) {
                return scrobble(profileId, action, event);
            });
        QObject::connect(
            auth, &TraktAuth::stateChanged, registry,
            [registry, auth] {
                registry->setProviderAuthenticated(TrackingProvider::Trakt,
                                                   auth->authenticated());
            });
        registry->setProviderAuthenticated(TrackingProvider::Trakt,
                                           auth->authenticated());
    }
}

bool TraktScrobbler::scrobble(int profileId, ScrobbleAction action,
                              const ScrobbleEvent& event)
{
    if (profileId != nuvio::settings::ActiveProfile::id()) return true;
    if (!m_auth || itemKey(event.media).isEmpty()) return true;
    const QString actionName = scrobbleWireValue(action);
    const double progress =
        std::clamp(event.progressPercent, 0.0, 100.0);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = itemKey(event.media);
    // Throttle (Compose shouldSkip parity): same window + profile + action
    // + item + near progress collapses, except stop-after-start.
    const bool sameWindow = now - m_lastTimestampMs < kMinIntervalMs;
    if (!(actionName == QLatin1String("stop") &&
          m_lastAction == QLatin1String("start") &&
          key == m_lastItemKey && profileId == m_lastProfile) &&
        sameWindow && profileId == m_lastProfile &&
        actionName == m_lastAction && key == m_lastItemKey &&
        std::fabs(m_lastProgress - progress) <= kProgressWindow)
        return true;
    m_lastProfile = profileId;
    m_lastAction = actionName;
    m_lastItemKey = key;
    m_lastProgress = progress;
    m_lastTimestampMs = now;
    send(actionName, event.media, progress, 1);
    return true;
}

void TraktScrobbler::send(const QString& action, const TrackingMedia& media,
                          double progress, int attempt)
{
    if (!m_auth) return;
    const QString bearer = m_auth->bearerToken();
    if (bearer.isEmpty()) return;   // signed out / refreshing: skip round
    QNetworkRequest req{QUrl(QString::fromLatin1(kBase) +
                             QStringLiteral("/scrobble/") + action)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", "Bearer " + bearer.toUtf8());
    const QString clientId =
        qEnvironmentVariable("NUVIO_TRAKT_CLIENT_ID");
    if (!clientId.isEmpty())
        req.setRawHeader("trakt-api-key", clientId.toUtf8());
    QNetworkAccessManager* nam = new QNetworkAccessManager(this);
    QNetworkReply* rep = nam->post(
        req, QJsonDocument(traktScrobbleBody(media, progress, m_appVersion))
                 .toJson(QJsonDocument::Compact));
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, nam, action, media, progress, attempt] {
                rep->deleteLater();
                nam->deleteLater();
                const int status = rep->attribute(
                                           QNetworkRequest::
                                               HttpStatusCodeAttribute)
                                       .toInt();
                if (status >= 200 && status < 300) return;
                if (rep->error() == QNetworkReply::AuthenticationRequiredError ||
                    status == 401 || status == 403) {
                    qCWarning(lcTrakt) << "scrobble" << action
                                       << "unauthorized; signing Trakt out";
                    if (m_auth) m_auth->signOut();
                    return;
                }
                if (action == QLatin1String("stop") &&
                    attempt < kMaxStopAttempts) {
                    // Overloaded servers back off longer (Compose parity).
                    const int delayMs = (status == 429 || status == 503 ||
                                         status == 502 || status == 504)
                                            ? 5000
                                            : 1500;
                    qCWarning(lcTrakt)
                        << "scrobble stop failed, retrying" << attempt;
                    QTimer::singleShot(delayMs, this,
                                       [this, action, media, progress,
                                        attempt] {
                                           send(action, media, progress,
                                                attempt + 1);
                                       });
                    return;
                }
                qCWarning(lcTrakt)
                    << "scrobble" << action << "failed:" << status;
            });
}

} // namespace nuvio::tracking
