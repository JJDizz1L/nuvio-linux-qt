#pragma once

// Skip-intro resolution (P3c): verbatim port of Compose's skip provider
// fan-out minus the Simkl-dependent legs. Direct ids resolve today:
//   tt... (+S/E) -> IntroDb (only when a base URL is configured; Compose
//     bakes "" by default and this line reads NUVIO_INTRODB_URL, same blank
//     default), mal:<id> -> AniSkip (public, keyless).
// kitsu: ids and anilist GraphQL need SimklIdResolver (SIMKL API + tracking
// auth) — honestly empty until the tracking backlog lands (Appendix A).
// Merge keeps first-wins per category (opening/recap/ending), providers
// queried in parallel with an overall guard. Results cache per content key.
// Submit posts IntroDb segments with the stored API key (Bearer).

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

#include "nuvio/playback/NextEpisodeRules.h"

namespace nuvio::playback {

/// IntroDb GET url ("" when unconfigured = leg disabled, Compose parity).
[[nodiscard]] QString introDbSegmentsUrl(const QString& baseUrl,
                                         const QString& imdbId, int season,
                                         int episode);
[[nodiscard]] QString aniskipUrl(const QString& malId, int episode);
[[nodiscard]] QString introDbSubmitUrl(const QString& baseUrl);
/// Submit body {imdb_id,season,episode,start_sec,end_sec} (camelCase response
/// shapes, snake_case submit shape — verbatim Compose models).
[[nodiscard]] QByteArray introDbSubmitBody(const QString& imdbId, int season,
                                           int episode, double startSec,
                                           double endSec);

/// Parses provider bodies into skip intervals (tolerant: unknown keys
/// ignored, invalid segments dropped, never throws).
[[nodiscard]] QList<SkipSegment> parseIntroDbSegments(const QByteArray& body);
[[nodiscard]] QList<SkipSegment> parseAniSkipTimes(const QByteArray& body);

/// First-wins per category (opening <- intro|op|mixed-op, ending <-
/// outro|ed|mixed-ed|credits|ending, recap <- recap), provider order kept.
[[nodiscard]] QList<SkipSegment> mergeSkipIntervals(
    const QList<QList<SkipSegment>>& providerResults);

/// Canonical segment-completion key (Compose autoSkipKey parity).
[[nodiscard]] QString skipCompletionKey(const QString& provider,
                                        const QString& type, double startSec,
                                        double endSec);

class SkipResolver final : public QObject {
    Q_OBJECT

public:
    struct Providers {
        std::function<bool()> skipIntroEnabled = [] { return true; };
        std::function<QString()> introDbBaseUrl = [] { return QString(); };
        std::function<QString()> introDbApiKey = [] { return QString(); };
        std::function<bool()> introSubmitEnabled = [] { return false; };
    };

    explicit SkipResolver(QObject* parent = nullptr);

    void setProviders(Providers providers);

    /// Resolves intervals for a content identity (composite tt:S:E, mal:,
    /// kitsu: accepted; season/episode args fill tt gaps). Emits intervals()
    /// exactly once per call (possibly empty); cached keys answer sync-fast.
    Q_INVOKABLE void resolve(const QString& id, int season = -1,
                             int episode = -1);
    /// Submits a user-marked segment for the LAST resolved tt identity.
    /// Preconditions (else submits nothing): submit enabled + API key set +
    /// last identity was tt. Emits submitted(ok).
    Q_INVOKABLE void submit(double startSec, double endSec);

signals:
    /// Intervals for the requested key ({startSec,endSec,type,provider}).
    void intervals(const QVariantList& segments);
    void submitted(bool ok);

private:
    void finishGuard();
    void ingest(QList<SkipSegment> segs);

    Providers m_providers;
    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, QList<SkipSegment>> m_cache;
    // Active lookup state (single-flight; a new resolve supersedes).
    quint64 m_token = 0;
    QString m_key;
    QList<QList<SkipSegment>> m_pending;
    int m_awaiting = 0;
    QString m_submitImdb;
    int m_submitSeason = -1;
    int m_submitEpisode = -1;
};

} // namespace nuvio::playback
