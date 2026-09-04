#pragma once

// TMDB id-resolution service (fork features/tmdb/TmdbService parity):
// imdb<->tmdb mapping over api.themoviedb.org/3 with in-memory caches.
// Async (QNAM) with value callbacks; silent nulls on missing key or
// network failure (callers treat null as "no enrichment").

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <functional>

namespace nuvio::tmdb {

class TmdbSettings;

/// "movie"/"tv" normalization ("film"->movie; tv/series/show/tvshow->tv).
[[nodiscard]] QString normalizeMediaType(const QString& mediaType);
/// Prefix-strip chain for incoming video ids (tmdb:/movie:/series:,
/// then composite/query tails).
[[nodiscard]] QString normalizeVideoId(const QString& videoId);
/// api.themoviedb.org/3/<endpoint>?api_key=..[&..] (blank values dropped).
[[nodiscard]] QString buildTmdbUrl(const QString& base,
                                   const QString& endpoint,
                                   const QString& apiKey,
                                   const QList<QPair<QString, QString>>& query = {});
/// First positive movie/tv result id from a /find body, per type rule.
[[nodiscard]] QString parseFindResult(const QByteArray& body,
                                      const QString& normalizedType);
/// imdb_id from an external_ids body (blank when absent).
[[nodiscard]] QString parseExternalIds(const QByteArray& body);

class TmdbService final : public QObject {
    Q_OBJECT

public:
    using IdCallback = std::function<void(const QString&)>;

    explicit TmdbService(TmdbSettings* settings, QObject* parent = nullptr);

    /// tmdb id for an imdb/composite id (numeric ids pass through;
    /// non-tt ids resolve empty). Cached per (id,type).
    void ensureTmdbId(const QString& videoId, const QString& mediaType,
                      IdCallback done);
    /// imdb id for a tmdb id. Cached per (id,type).
    void tmdbToImdb(int tmdbId, const QString& mediaType, IdCallback done);

    /// Test seam: base url override (default api.themoviedb.org/3).
    void setBaseUrl(const QString& base) { m_base = base; }

private:
    void fetch(const QString& endpoint,
               const QList<QPair<QString, QString>>& query,
               std::function<void(const QByteArray&)> done);

    TmdbSettings* m_settings = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QString m_base = QStringLiteral("https://api.themoviedb.org/3");
    QHash<QString, QString> m_imdbToTmdb;
    QHash<QString, QString> m_tmdbToImdb;
};

} // namespace nuvio::tmdb
