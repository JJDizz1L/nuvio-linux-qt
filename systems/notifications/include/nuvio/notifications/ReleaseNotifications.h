#pragma once

// Episode-release notifications (fork features/notifications parity).
// The data plane is verbatim: tracked-show keys, series-type
// normalization, JVM-hash notification ids, S1E2 body shapes,
// nuvio:// deep links, followedOn inference (savedAt >= 1999-12-31 else
// today), followedOn-gated request building over Cinemeta meta bodies.
//
// One honest platform divergence: the fork's desktop backend is a stub
// (authorization always denied, schedule/show are no-ops, and the page
// is hidden behind AppFeaturePolicy). Linux has a real notification
// bus, so this line ships a notify-send backend instead: enabling
// probes for notify-send, refresh fires due releases (releaseDate <=
// today) once each (fired ids persist Qt-locally), future releases
// count as scheduled and fire on later refreshes. The settings leaf is
// therefore visible here, unlike fork-desktop.

#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>

namespace nuvio::library {
class LibraryStore;
class MetaService;
} // namespace nuvio::library

namespace nuvio::notifications {

/// JVM String.hashCode over UTF-16 units (notification-id parity).
[[nodiscard]] qint32 jvmStringHash(const QString& s);
/// Series-type normalization ("tv"/"show"/"tvshow" -> "series").
[[nodiscard]] QString normalizeSeriesType(const QString& type);
/// "<normalized-type>:<trimmed-id>".
[[nodiscard]] QString buildTrackedShowKey(const QString& type,
                                          const QString& id);
[[nodiscard]] bool isSeriesLibraryType(const QString& type);
/// "episode-release-<profile>-<abs(contentHash)>-<abs(episodeHash)>-<iso>".
[[nodiscard]] QString buildNotificationId(int profileId,
                                          const QString& contentType,
                                          const QString& contentId,
                                          const QString& episodeId,
                                          const QString& releaseDateIso);
/// "S1E2 • Title is out now" and its three fallbacks (fork string
/// resources verbatim: S%1dE%2d / E%1d code shapes, no zero-padding).
[[nodiscard]] QString buildNotificationBody(int seasonNumber,
                                            int episodeNumber,
                                            const QString& episodeTitle);
/// "nuvio://meta?type=..&id=.." (deeplink route lands later; the string
/// already rides the request for parity).
[[nodiscard]] QString buildMetaDeepLinkUrl(const QString& type,
                                           const QString& id);

struct TrackedShow {
    QString contentId;
    QString contentType;
    QString followedOnIsoDate;
};

struct ReleaseRequest {
    QString requestId;
    QString notificationTitle;
    QString notificationBody;
    QString releaseDateIso;
    QString deepLinkUrl;
    QString backdropUrl;
};

/// Pure request builder over one meta body (metaFromJson shape): keeps
/// episodes with a parsable release on/after followedOn and a usable
/// season/episode identity.
[[nodiscard]] QList<ReleaseRequest>
buildRequestsForShow(int profileId, const TrackedShow& show,
                     const QString& showTitle,
                     const QVariantList& videos);

/// kotlinx-parity codec (encodeDefaults, ignoreUnknownKeys, garbage→empty).
[[nodiscard]] QString encodePayload(bool enabled,
                                    const QList<TrackedShow>& shows);
struct DecodedPayload {
    bool enabled = false;
    QList<TrackedShow> shows;
};
[[nodiscard]] DecodedPayload decodePayload(const QString& raw);

class ReleaseNotificationManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool permissionGranted READ permissionGranted NOTIFY changed)
    Q_PROPERTY(int scheduledCount READ scheduledCount NOTIFY changed)
    Q_PROPERTY(QString testTargetTitle READ testTargetTitle NOTIFY changed)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)

public:
    explicit ReleaseNotificationManager(nuvio::library::LibraryStore* library,
                                        QObject* parent = nullptr);

    [[nodiscard]] bool enabled() const { return m_enabled; }
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] bool permissionGranted() const
    {
        return m_permissionGranted;
    }
    [[nodiscard]] int scheduledCount() const { return m_scheduledCount; }
    [[nodiscard]] QString testTargetTitle() const
    {
        return m_testTargetTitle;
    }
    [[nodiscard]] QString statusMessage() const { return m_statusMessage; }
    [[nodiscard]] QString errorMessage() const { return m_errorMessage; }

    /// Sync-blob owned feature (notifications_settings parity).
    [[nodiscard]] QJsonObject exportSyncPayload() const;
    /// Returns true when the remote value changed local state.
    bool applySyncPayload(const QJsonObject& payload);
    /// Remote-wins setter shared by the sync leg (fork applyFromSync).
    Q_INVOKABLE void applyFromSyncEnabled(bool enabled);

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void sendTestNotification();
    Q_INVOKABLE void refreshAsync();

    /// Profile switches (P7): aborts in-flight meta fetches, reloads.
    Q_INVOKABLE void setProfileId(int profileId);

signals:
    void changed();

private:
    void load();
    void persist();
    void updateTestTarget();
    // Reconciles tracked rows against the library (fork
    // reconcileTrackedShows: series items only, keeps existing
    // followedOn dates). Returns true when the set changed.
    bool reconcile();
    void finishRefresh(const QList<ReleaseRequest>& requests);
    void fireDue(const QList<ReleaseRequest>& requests,
                 const QString& today);
    void pumpQueue();   // width-4 meta fetch chain over m_queue
    static bool notifySendAvailable();

    nuvio::library::LibraryStore* m_library = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QByteArray m_baseUrl;

    int m_profileId = 1;
    bool m_enabled = false;
    bool m_loading = false;
    bool m_permissionGranted = false;
    int m_scheduledCount = 0;
    QString m_testTargetTitle;
    QString m_statusMessage;
    QString m_errorMessage;

    QList<TrackedShow> m_shows;
    QStringList m_firedIds;   // Qt-local: already-fired request ids
    QList<TrackedShow> m_queue;        // refresh fetch backlog (member:
    QList<ReleaseRequest> m_collecting; // async replies outlive the call)
    int m_inflight = 0;       // meta fetches outstanding (cap 4)
    int m_refreshToken = 0;   // stale-guard across overlapping refreshes
    bool m_loaded = false;
};

} // namespace nuvio::notifications
