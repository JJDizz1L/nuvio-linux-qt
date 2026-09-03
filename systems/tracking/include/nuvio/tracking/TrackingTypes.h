#pragma once

// Tracking domain vocabulary (T1): verbatim port of Compose's tracking
// model layer (TrackingProviderId storage ids, capabilities, media
// reference with external ids, scrobble actions with wire values, seek
// policy). Pure data; behavior lives in TrackingRegistry / coordinator.

#include <QList>
#include <QObject>
#include <QString>

namespace nuvio::tracking {

enum class TrackingProvider {
    Trakt,
    Simkl,
};

[[nodiscard]] inline QString providerStorageId(TrackingProvider p)
{
    return p == TrackingProvider::Trakt ? QStringLiteral("trakt")
                                        : QStringLiteral("simkl");
}

[[nodiscard]] inline QString providerDisplayName(TrackingProvider p)
{
    return p == TrackingProvider::Trakt ? QStringLiteral("Trakt")
                                        : QStringLiteral("SIMKL");
}

enum class TrackingCapability {
    Authentication,
    LibraryRead,
    LibraryWrite,
    WatchedRead,
    WatchedWrite,
    ProgressRead,
    ProgressWrite,
    Scrobble,
    Comments,
    Recommendations,
};

enum class TrackingMediaKind {
    Movie,
    Show,
    Anime,
};

struct TrackingExternalIds {
    QString imdb;   // "tt..." (our primary identity; Trakt accepts it)
    qint64 tmdb = -1;
    QString tvdb;
    qint64 trakt = -1;
    qint64 simkl = -1;
    qint64 mal = -1;
    qint64 anidb = -1;
    qint64 anilist = -1;
    qint64 kitsu = -1;
};

struct TrackingEpisode {
    int season = -1;
    int number = -1;
    QString title;
};

struct TrackingMedia {
    TrackingMediaKind kind = TrackingMediaKind::Movie;
    QString title;
    int year = -1;
    TrackingExternalIds ids;
    TrackingEpisode episode;   // valid when kind == Episode
};

enum class ScrobbleAction {
    Start,
    Pause,
    Stop,
};

[[nodiscard]] inline QString scrobbleWireValue(ScrobbleAction action)
{
    switch (action) {
    case ScrobbleAction::Start: return QStringLiteral("start");
    case ScrobbleAction::Pause: return QStringLiteral("pause");
    case ScrobbleAction::Stop: return QStringLiteral("stop");
    }
    return QStringLiteral("stop");
}

enum class SeekScrobblePolicy {
    None,
    StopAndRestart,
};

struct ScrobbleEvent {
    TrackingMedia media;
    double progressPercent = 0.0;
};

/// Builds scrobble media from a playback identity. Series ids ride the
/// composite "tt:S:E" form (shell/session convention); everything else is
/// a movie. Year is unknown on this line (omitted, never guessed).
[[nodiscard]] inline TrackingMedia mediaForPlayback(const QString& type,
                                                   const QString& id,
                                                   const QString& title)
{
    TrackingMedia m;
    m.title = title;
    const QStringList parts = id.split(u':');
    // Compose kinds are MOVIE/SHOW/ANIME; episodes are SHOW/ANIME with an
    // episode coordinate (never a separate kind).
    if (type == QLatin1String("series") || type == QLatin1String("anime")) {
        m.kind = type == QLatin1String("anime") ? TrackingMediaKind::Anime
                                                : TrackingMediaKind::Show;
        if (parts.size() == 3) {
            bool okS = false, okE = false;
            const int s = parts[1].toInt(&okS);
            const int e = parts[2].toInt(&okE);
            if (okS && okE && parts[0].startsWith(QLatin1String("tt"))) {
                m.ids.imdb = parts[0];
                m.episode.season = s;
                m.episode.number = e;
                return m;
            }
        }
        if (id.startsWith(QLatin1String("mal:"), Qt::CaseInsensitive)) {
            bool ok = false;
            const qint64 mal = id.section(u':', 1, 1).toLongLong(&ok);
            if (ok) m.ids.mal = mal;
        }
        return m;
    }
    if (id.startsWith(QLatin1String("tt"))) m.ids.imdb = id;
    return m;
}

} // namespace nuvio::tracking
