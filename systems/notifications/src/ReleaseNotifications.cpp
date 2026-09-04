#include "nuvio/notifications/ReleaseNotifications.h"

#include <algorithm>

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include "nuvio/library/LibraryStore.h"
#include "nuvio/library/MetaService.h"
#include "nuvio/notifications/ReleaseDate.h"
#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::notifications {

namespace {

// savedAt before 1999-12-31T00:00Z is legacy-missing data (fork
// MinReasonableSavedAtEpochMs); those rows follow "today".
constexpr qint64 kMinReasonableSavedAtEpochMs = 946684800000LL;
constexpr int kFetchConcurrency = 4;   // fork metadataFetchConcurrency
constexpr auto kStoreFile = "episode_release_notifications";

[[nodiscard]] QString storeKey(int profileId)
{
    return QStringLiteral("episode_release_notifications_") +
           QString::number(profileId);
}

[[nodiscard]] QString followedOnFor(qint64 savedAtEpochMs)
{
    if (savedAtEpochMs >= kMinReasonableSavedAtEpochMs)
        return isoDateFromEpochMs(savedAtEpochMs);
    return todayIsoDate();
}

} // namespace

qint32 jvmStringHash(const QString& s)
{
    // Kotlin String.hashCode: h = 31*h + utf16Unit, int32 wraparound.
    quint32 h = 0;
    for (const QChar c : s) h = 31 * h + static_cast<quint16>(c.unicode());
    return static_cast<qint32>(h);
}

QString normalizeSeriesType(const QString& type)
{
    const QString t = type.trimmed().toLower();
    if (t == QLatin1String("tv") || t == QLatin1String("show") ||
        t == QLatin1String("series") || t == QLatin1String("tvshow"))
        return QStringLiteral("series");
    return t;
}

QString buildTrackedShowKey(const QString& type, const QString& id)
{
    return normalizeSeriesType(type) + u':' + id.trimmed();
}

bool isSeriesLibraryType(const QString& type)
{
    return normalizeSeriesType(type) == QLatin1String("series");
}

QString buildNotificationId(int profileId, const QString& contentType,
                            const QString& contentId,
                            const QString& episodeId,
                            const QString& releaseDateIso)
{
    const qint32 contentHash =
        jvmStringHash(buildTrackedShowKey(contentType, contentId));
    const QString ep =
        episodeId.trimmed().isEmpty() ? releaseDateIso : episodeId.trimmed();
    const qint32 episodeHash = jvmStringHash(ep);
    // Kotlin abs(): MIN_VALUE stays (two's-complement wraparound parity).
    const auto abs32 = [](qint32 v) -> qint64 {
        return v == INT32_MIN ? static_cast<qint64>(INT32_MIN)
                              : (v < 0 ? -static_cast<qint64>(v) : v);
    };
    return QStringLiteral("episode-release-%1-%2-%3-%4")
        .arg(profileId)
        .arg(abs32(contentHash))
        .arg(abs32(episodeHash))
        .arg(releaseDateIso);
}

QString buildNotificationBody(int seasonNumber, int episodeNumber,
                              const QString& episodeTitle)
{
    // Fork code shapes: full "S%1dE%2d", episode-only "E%1d".
    QString code;
    if (seasonNumber >= 0 && episodeNumber >= 0)
        code = QStringLiteral("S%1E%2").arg(seasonNumber).arg(episodeNumber);
    else if (episodeNumber >= 0)
        code = QStringLiteral("E%1").arg(episodeNumber);
    const QString title = episodeTitle.trimmed();
    if (!code.isEmpty() && !title.isEmpty())
        return QStringLiteral("%1 • %2 is out now").arg(code, title);
    if (!code.isEmpty())
        return QStringLiteral("%1 is out now").arg(code);
    if (!title.isEmpty())
        return QStringLiteral("%1 is out now").arg(title);
    return QStringLiteral("A new episode is out now");
}

QString buildMetaDeepLinkUrl(const QString& type, const QString& id)
{
    return QStringLiteral("nuvio://meta?type=%1&id=%2")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(type.trimmed())),
             QString::fromUtf8(QUrl::toPercentEncoding(id.trimmed())));
}

QList<ReleaseRequest> buildRequestsForShow(int profileId,
                                           const TrackedShow& show,
                                           const QString& showTitle,
                                           const QVariantList& videos)
{
    QList<ReleaseRequest> out;
    for (const QVariant& entry : videos) {
        const QVariantMap ep = entry.toMap();
        const QString release =
            parseEpisodeReleaseLocalDate(ep.value("released").toString());
        if (release.isEmpty()) continue;
        if (release < show.followedOnIsoDate) continue;
        const int season = ep.value("season", -1).toInt();
        const int episode = ep.value("episode", -1).toInt();
        if (season < 0 && episode < 0) continue;
        const QString epId = ep.value("id").toString();
        ReleaseRequest req;
        req.requestId = buildNotificationId(profileId, show.contentType,
                                            show.contentId, epId, release);
        req.notificationTitle =
            showTitle.trimmed().isEmpty() ? show.contentId : showTitle;
        req.notificationBody =
            buildNotificationBody(season, episode, ep.value("name").toString());
        req.releaseDateIso = release;
        req.deepLinkUrl =
            buildMetaDeepLinkUrl(show.contentType, show.contentId);
        // Backdrop pick order (fork): meta background, episode
        // thumbnail, season poster, meta poster. metaFromJson exposes
        // background/poster top-level and thumb per video; season
        // posters are not carried, so that leg is skipped by shape.
        req.backdropUrl = ep.value("thumb").toString();
        out.append(req);
    }
    return out;
}

QString encodePayload(bool enabled, const QList<TrackedShow>& shows)
{
    QList<TrackedShow> sorted = shows;
    std::sort(sorted.begin(), sorted.end(), [](const TrackedShow& a,
                                               const TrackedShow& b) {
        if (a.contentType != b.contentType) return a.contentType < b.contentType;
        return a.contentId < b.contentId;
    });
    QJsonArray arr;
    for (const TrackedShow& s : sorted) {
        arr.append(QJsonObject{
            {QStringLiteral("contentId"), s.contentId},
            {QStringLiteral("contentType"), s.contentType},
            {QStringLiteral("followedOnIsoDate"), s.followedOnIsoDate},
        });
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("followedShows"), arr},
    }).toJson(QJsonDocument::Compact));
}

DecodedPayload decodePayload(const QString& raw)
{
    DecodedPayload out;
    if (raw.trimmed().isEmpty()) return out;
    const QJsonObject root =
        QJsonDocument::fromJson(raw.toUtf8()).object();
    if (root.isEmpty()) return out;   // garbage -> empty (runCatching)
    out.enabled = root.value(QStringLiteral("enabled")).toBool(false);
    for (const QJsonValue& v :
         root.value(QStringLiteral("followedShows")).toArray()) {
        const QJsonObject o = v.toObject();
        TrackedShow s;
        s.contentId = o.value(QStringLiteral("contentId")).toString();
        s.contentType = o.value(QStringLiteral("contentType")).toString();
        s.followedOnIsoDate =
            o.value(QStringLiteral("followedOnIsoDate")).toString();
        if (s.contentId.isEmpty()) continue;
        out.shows.append(s);
    }
    return out;
}

// ---- manager ------------------------------------------------------------

ReleaseNotificationManager::ReleaseNotificationManager(
    nuvio::library::LibraryStore* library, QObject* parent)
    : QObject(parent),
      m_library(library),
      m_nam(new QNetworkAccessManager(this)),
      m_baseUrl(qgetenv("NUVIO_CINEMETA_BASE")),
      m_profileId(nuvio::settings::ActiveProfile::id())
{
    if (m_baseUrl.isEmpty())
        m_baseUrl = "https://v3-cinemeta.strem.io";
    Q_ASSERT(m_library);
    connect(m_library, &nuvio::library::LibraryStore::changed, this,
            [this] {
                if (!m_loaded) return;
                if (reconcile()) persist();
                updateTestTarget();
                emit changed();
                if (m_enabled) refreshAsync();
            });
}

QJsonObject ReleaseNotificationManager::exportSyncPayload() const
{
    return QJsonObject{
        {QStringLiteral("episodeReleaseAlertsEnabled"), m_enabled},
    };
}

bool ReleaseNotificationManager::applySyncPayload(const QJsonObject& payload)
{
    if (!payload.contains(QStringLiteral("episodeReleaseAlertsEnabled")))
        return false;   // absent keys untouched (owned-feature merge rule)
    const bool enabled =
        payload.value(QStringLiteral("episodeReleaseAlertsEnabled"))
            .toBool(false);
    if (!m_loaded) load();
    if (m_enabled == enabled) return false;
    applyFromSyncEnabled(enabled);
    return true;
}

void ReleaseNotificationManager::applyFromSyncEnabled(bool enabled)
{
    if (!m_loaded) load();
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    m_loading = false;
    m_statusMessage.clear();
    m_errorMessage.clear();
    persist();
    emit changed();
    refreshAsync();
}

void ReleaseNotificationManager::setEnabled(bool enabled)
{
    if (!m_loaded) load();
    if (!enabled) {
        m_enabled = false;
        m_loading = false;
        m_scheduledCount = 0;
        m_statusMessage.clear();
        m_errorMessage.clear();
        persist();
        emit changed();
        return;
    }
    // No OS permission model on Linux: capability probe instead. Absent
    // notify-send reads exactly like the fork's denied-authorization leg.
    if (!notifySendAvailable()) {
        m_enabled = false;
        m_loading = false;
        m_permissionGranted = false;
        m_scheduledCount = 0;
        m_statusMessage.clear();
        m_errorMessage = tr("System notifications are disabled for Nuvio. "
                            "Enable them to receive alerts and test "
                            "notifications.");
        persist();
        emit changed();
        return;
    }
    m_enabled = true;
    m_loading = false;
    m_permissionGranted = true;
    m_statusMessage.clear();
    m_errorMessage.clear();
    persist();
    emit changed();
    refreshAsync();
}

void ReleaseNotificationManager::sendTestNotification()
{
    if (!m_loaded) load();
    QString targetName;
    QString targetBackdrop;
    if (m_library) {
        const QList<nuvio::library::LibraryItem> items = m_library->items();
        const auto* pick = static_cast<const nuvio::library::LibraryItem*>(
            nullptr);
        for (const auto& it : items) {
            if (isSeriesLibraryType(it.type)) {
                pick = &it;
                break;
            }
        }
        if (!pick && !items.isEmpty()) pick = &items.first();
        if (pick) {
            targetName = pick->name.trimmed().isEmpty() ? pick->id : pick->name;
            targetBackdrop = pick->poster;
        }
    }
    if (targetName.isEmpty()) {
        m_statusMessage.clear();
        m_errorMessage =
            tr("Save a show to your library first to test notifications.");
        emit changed();
        return;
    }
    if (!notifySendAvailable()) {
        m_permissionGranted = false;
        m_statusMessage.clear();
        m_errorMessage = tr("System notifications are disabled for Nuvio. "
                            "Enable them to receive alerts and test "
                            "notifications.");
        emit changed();
        return;
    }
    const bool fired = QProcess::startDetached(
        QStringLiteral("notify-send"),
        {QStringLiteral("-a"), QStringLiteral("Nuvio"), targetName,
         tr("Preview episode release alert.")});
    if (fired) {
        m_permissionGranted = true;
        m_statusMessage = tr("Test notification sent for %1.").arg(targetName);
        m_errorMessage.clear();
    } else {
        m_permissionGranted = true;
        m_statusMessage.clear();
        m_errorMessage = tr("Failed to send a test notification.");
    }
    Q_UNUSED(targetBackdrop);
    emit changed();
}

void ReleaseNotificationManager::refreshAsync()
{
    if (!m_loaded) load();
    if (m_loading) return;   // single-flight (fork refreshMutex parity)
    if (reconcile()) persist();
    updateTestTarget();
    if (!m_enabled) {
        m_scheduledCount = 0;
        emit changed();
        return;
    }
    const bool granted = notifySendAvailable();
    m_permissionGranted = granted;
    if (!granted) {
        m_scheduledCount = 0;
        m_errorMessage = tr("System notifications are disabled for Nuvio. "
                            "Enable them to receive alerts and test "
                            "notifications.");
        emit changed();
        return;
    }
    if (m_shows.isEmpty()) {
        m_loading = false;
        m_scheduledCount = 0;
        m_errorMessage.clear();
        emit changed();
        return;
    }
    m_loading = true;
    m_errorMessage.clear();
    emit changed();
    ++m_refreshToken;
    m_queue = m_shows;
    m_collecting.clear();
    m_inflight = 0;
    for (int i = 0; i < kFetchConcurrency; ++i) pumpQueue();
}

void ReleaseNotificationManager::pumpQueue()
{
    const int token = m_refreshToken;
    if (m_queue.isEmpty()) {
        if (m_inflight == 0 && m_loading) finishRefresh(m_collecting);
        return;
    }
    const TrackedShow show = m_queue.takeFirst();
    ++m_inflight;
    QNetworkRequest req{QUrl(
        QString::fromUtf8(m_baseUrl) + QStringLiteral("/meta/") +
        QUrl::toPercentEncoding(show.contentType.trimmed()) + u'/' +
        QUrl::toPercentEncoding(show.contentId.trimmed()) +
        QStringLiteral(".json"))};
    QNetworkReply* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, show, token] {
                rep->deleteLater();
                --m_inflight;
                if (token == m_refreshToken &&
                    rep->error() == QNetworkReply::NoError) {
                    const QVariantMap meta =
                        nuvio::library::MetaService::metaFromJson(
                            rep->readAll());
                    QList<ReleaseRequest> reqs = buildRequestsForShow(
                        m_profileId, show,
                        meta.value(QStringLiteral("name")).toString(),
                        meta.value(QStringLiteral("videos")).toList());
                    // Backdrop backfill (fork pick order): meta
                    // background wins, then poster; per-video thumb
                    // already set by the builder.
                    const QString bg =
                        meta.value(QStringLiteral("background")).toString();
                    const QString poster =
                        meta.value(QStringLiteral("poster")).toString();
                    for (ReleaseRequest& r : reqs) {
                        if (r.backdropUrl.isEmpty())
                            r.backdropUrl = !bg.isEmpty() ? bg : poster;
                        m_collecting.append(r);
                    }
                }
                if (token != m_refreshToken) return;   // superseded
                pumpQueue();
            });
}

void ReleaseNotificationManager::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    ++m_refreshToken;   // drops late replies from the old profile
    m_profileId = profileId;
    m_loading = false;
    m_inflight = 0;
    load();
    emit changed();
}

void ReleaseNotificationManager::load()
{
    m_loaded = true;
    m_shows.clear();
    m_firedIds.clear();
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStoreFile));
    const auto raw = store.getString(storeKey(m_profileId).toStdString());
    const DecodedPayload payload =
        decodePayload(raw ? QString::fromStdString(*raw) : QString());
    m_enabled = payload.enabled;
    m_shows = payload.shows;
    const auto fired = store.getString(
        (storeKey(m_profileId) + QStringLiteral("_fired")).toStdString());
    if (fired) {
        for (const QJsonValue& v :
             QJsonDocument::fromJson(
                 QByteArray::fromStdString(*fired)).array())
            m_firedIds.append(v.toString());
    }
    updateTestTarget();
}

void ReleaseNotificationManager::persist()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStoreFile));
    store.putString(storeKey(m_profileId).toStdString(),
                    encodePayload(m_enabled, m_shows).toStdString());
    QJsonArray fired;
    for (const QString& id : m_firedIds) fired.append(id);
    store.putString((storeKey(m_profileId) + QStringLiteral("_fired"))
                        .toStdString(),
                    QString::fromUtf8(QJsonDocument(fired).toJson(
                                          QJsonDocument::Compact))
                        .toStdString());
}

void ReleaseNotificationManager::updateTestTarget()
{
    m_testTargetTitle.clear();
    if (!m_library) return;
    const QList<nuvio::library::LibraryItem> items = m_library->items();
    for (const auto& it : items) {
        if (isSeriesLibraryType(it.type)) {
            m_testTargetTitle =
                it.name.trimmed().isEmpty() ? it.id : it.name;
            return;
        }
    }
    if (!items.isEmpty()) {
        const auto& it = items.first();
        m_testTargetTitle = it.name.trimmed().isEmpty() ? it.id : it.name;
    }
}

bool ReleaseNotificationManager::reconcile()
{
    if (!m_library) return false;
    QList<TrackedShow> next;
    for (const nuvio::library::LibraryItem& item : m_library->items()) {
        if (!isSeriesLibraryType(item.type)) continue;
        const QString key = buildTrackedShowKey(item.type, item.id);
        auto kept = std::find_if(m_shows.begin(), m_shows.end(),
                                 [&](const TrackedShow& s) {
                                     return buildTrackedShowKey(
                                                s.contentType,
                                                s.contentId) == key;
                                 });
        if (kept != m_shows.end()) {
            next.append(*kept);
        } else {
            TrackedShow fresh;
            fresh.contentId = item.id;
            fresh.contentType = item.type;
            fresh.followedOnIsoDate = followedOnFor(item.savedAtEpochMs);
            next.append(fresh);
        }
    }
    if (next.size() == m_shows.size()) {
        bool same = true;
        for (int i = 0; i < next.size(); ++i) {
            if (next[i].contentId != m_shows[i].contentId ||
                next[i].contentType != m_shows[i].contentType ||
                next[i].followedOnIsoDate != m_shows[i].followedOnIsoDate) {
                same = false;
                break;
            }
        }
        if (same) return false;
    }
    m_shows = next;
    return true;
}

void ReleaseNotificationManager::finishRefresh(
    const QList<ReleaseRequest>& requests)
{
    m_loading = false;
    m_inflight = 0;
    // Dedupe by request id (a show re-fetched across overlapping runs
    // must not double-count).
    QList<ReleaseRequest> unique;
    QStringList seen;
    for (const ReleaseRequest& r : requests) {
        if (seen.contains(r.requestId)) continue;
        seen.append(r.requestId);
        unique.append(r);
    }
    m_scheduledCount = unique.size();
    fireDue(unique, todayIsoDate());
    persist();
    updateTestTarget();
    emit changed();
}

void ReleaseNotificationManager::fireDue(const QList<ReleaseRequest>& requests,
                                         const QString& today)
{
    bool firedAny = false;
    for (const ReleaseRequest& r : requests) {
        if (r.releaseDateIso > today) continue;   // future: stays scheduled
        if (m_firedIds.contains(r.requestId)) continue;
        const bool ok = QProcess::startDetached(
            QStringLiteral("notify-send"),
            {QStringLiteral("-a"), QStringLiteral("Nuvio"),
             r.notificationTitle, r.notificationBody});
        if (ok) {
            m_firedIds.append(r.requestId);
            firedAny = true;
        }
    }
    Q_UNUSED(firedAny);
}

bool ReleaseNotificationManager::notifySendAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("notify-send"))
                .isEmpty();
}

} // namespace nuvio::notifications
