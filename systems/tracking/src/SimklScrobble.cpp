#include "nuvio/tracking/SimklScrobble.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkReply>

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/tracking/SimklAuth.h"

namespace nuvio::tracking {

namespace {
Q_LOGGING_CATEGORY(lcSimkl, "nuvio.tracking.simkl", QtWarningMsg)
constexpr auto kBase = "https://api.simkl.com";

[[nodiscard]] QJsonObject simklIds(const TrackingExternalIds& ids)
{
    QJsonObject o;
    if (ids.simkl >= 0) o.insert(QStringLiteral("simkl"), ids.simkl);
    if (!ids.imdb.trimmed().isEmpty())
        o.insert(QStringLiteral("imdb"), ids.imdb.trimmed());
    if (ids.tmdb >= 0) o.insert(QStringLiteral("tmdb"), ids.tmdb);
    if (!ids.tvdb.trimmed().isEmpty()) {
        bool ok = false;
        const qint64 v = ids.tvdb.trimmed().toLongLong(&ok);
        o.insert(QStringLiteral("tvdb"),
                 ok ? QJsonValue(v) : QJsonValue(ids.tvdb.trimmed()));
    }
    if (ids.mal >= 0) o.insert(QStringLiteral("mal"), ids.mal);
    if (ids.anidb >= 0) o.insert(QStringLiteral("anidb"), ids.anidb);
    if (ids.anilist >= 0) o.insert(QStringLiteral("anilist"), ids.anilist);
    if (ids.kitsu >= 0) o.insert(QStringLiteral("kitsu"), ids.kitsu);
    return o;
}

[[nodiscard]] double roundedProgress(double v)
{
    return std::round(std::clamp(v, 0.0, 100.0) * 100.0) / 100.0;
}
} // namespace

QJsonObject simklScrobbleBody(const TrackingMedia& media,
                              double progressPercent)
{
    QJsonObject body;
    body.insert(QStringLiteral("progress"),
                roundedProgress(progressPercent));
    QJsonObject dto;
    if (!media.title.trimmed().isEmpty())
        dto.insert(QStringLiteral("title"), media.title.trimmed());
    if (media.year >= 0) dto.insert(QStringLiteral("year"), media.year);
    const QJsonObject ids = simklIds(media.ids);
    if (!ids.isEmpty()) dto.insert(QStringLiteral("ids"), ids);
    const bool tvAnime = media.kind == TrackingMediaKind::Anime &&
                         media.episode.season >= 0;
    if (media.kind == TrackingMediaKind::Movie)
        body.insert(QStringLiteral("movie"), dto);
    else if (media.kind == TrackingMediaKind::Show || tvAnime)
        body.insert(QStringLiteral("show"), dto);
    else
        body.insert(QStringLiteral("anime"), dto);
    if (media.episode.number >= 0) {
        QJsonObject ep{{QStringLiteral("number"), media.episode.number}};
        if (media.episode.season >= 0)
            ep.insert(QStringLiteral("season"), media.episode.season);
        body.insert(QStringLiteral("episode"), ep);
    }
    return body;
}

SimklScrobbler::SimklScrobbler(SimklAuth* auth, TrackingRegistry* registry,
                               QObject* parent)
    : QObject(parent), m_auth(auth)
{
    if (registry) {
        registry->registerScrobbler(
            TrackingProvider::Simkl, SeekScrobblePolicy::None,
            [this](int profileId, ScrobbleAction action,
                   const ScrobbleEvent& event) {
                return scrobble(profileId, action, event);
            });
        QObject::connect(
            auth, &SimklAuth::stateChanged, registry,
            [registry, auth] {
                registry->setProviderAuthenticated(TrackingProvider::Simkl,
                                                   auth->authenticated());
            });
        registry->setProviderAuthenticated(TrackingProvider::Simkl,
                                           auth->authenticated());
    }
}

bool SimklScrobbler::scrobble(int profileId, ScrobbleAction action,
                              const ScrobbleEvent& event)
{
    if (profileId != nuvio::settings::ActiveProfile::id()) return true;
    if (!m_auth || m_auth->accessToken().isEmpty()) return true;
    // Simkl requires a resolvable identity (Compose validated() parity).
    if (simklIds(event.media.ids).isEmpty() &&
        event.media.title.trimmed().isEmpty())
        return true;
    QNetworkRequest req{QUrl(QString::fromLatin1(kBase) +
                             QStringLiteral("/scrobble/") +
                             scrobbleWireValue(action))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    const QString clientId =
        qEnvironmentVariable("NUVIO_SIMKL_CLIENT_ID");
    if (!clientId.isEmpty())
        req.setRawHeader("client_id", clientId.toUtf8());
    req.setRawHeader(
        "app-name",
        qEnvironmentVariable("NUVIO_SIMKL_APP_NAME", "nuvio").toUtf8());
    req.setRawHeader("Authorization",
                     "Bearer " + m_auth->accessToken().toUtf8());
    QNetworkAccessManager* nam = new QNetworkAccessManager(this);
    QNetworkReply* rep = nam->post(
        req, QJsonDocument(simklScrobbleBody(event.media,
                                             event.progressPercent))
                 .toJson(QJsonDocument::Compact));
    connect(rep, &QNetworkReply::finished, this, [rep, nam, action] {
        rep->deleteLater();
        nam->deleteLater();
        const int status =
            rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) return;
        if (status == 401 || status == 403) {
            qCWarning(lcSimkl) << "scrobble" << scrobbleWireValue(action)
                               << "unauthorized";
            return;
        }
        qCWarning(lcSimkl)
            << "scrobble" << scrobbleWireValue(action) << "failed:" << status;
    });
    return true;
}

} // namespace nuvio::tracking
