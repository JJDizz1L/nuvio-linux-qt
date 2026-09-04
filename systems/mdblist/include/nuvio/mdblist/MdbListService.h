#pragma once

// MDBList ratings service (fork features/mdblist/MdbListMetadataService
// parity): per-provider POST ratings over api.mdblist.com with an
// in-memory cache, priority-ordered fetch, display-ordered formatted
// rows for the detail page. Silent empties on missing key or network
// failure (callers treat empty as "no ratings").

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <optional>

namespace nuvio::mdblist {

class MdbListSettings;

/// Provider ids (verbatim) + fetch priority order.
extern const QStringList kProviderPriority;
/// Display order + labels + formats (DetailRatingsRow parity; the fork
/// renders logo drawables, this line renders text chips in source
/// colors since no rating-logo assets are ported).
struct RatingDisplay {
    QString source;
    QString label;
    QString color;   // hex
    enum class Format { OneDecimal, Whole, Percent } format;
};
extern const QList<RatingDisplay> kRatingDisplayOrder;

/// First tt\d+ match in a value (composite ids resolve to their head).
[[nodiscard]] QString extractImdbId(const QString& value);
/// "movie" stays movie, everything else is "show" (fork parity).
[[nodiscard]] QString toMdbListMediaType(const QString& metaType);
/// https://api.mdblist.com/rating/<media>/<provider>?apikey=<key>.
[[nodiscard]] QString ratingUrl(const QString& base,
                                const QString& mediaType,
                                const QString& providerId,
                                const QString& apiKey);
/// {"ids":[imdb],"provider":"imdb"} (lookup always by imdb id).
[[nodiscard]] QByteArray ratingRequestBody(const QString& imdbId);
/// First ratings[].rating (null when absent).
[[nodiscard]] std::optional<double> parseRating(const QByteArray& body);
/// Fork value formats verbatim (round-half-away like roundToInt).
[[nodiscard]] QString formatOneDecimal(double value);
[[nodiscard]] QString formatWhole(double value);
[[nodiscard]] QString formatPercent(double value);

class MdbListService final : public QObject {
    Q_OBJECT
    /// Display-ready rows: [{source,label,text,color}] in display order.
    Q_PROPERTY(QVariantList ratings READ ratings NOTIFY ratingsChanged)

public:
    using RatingsCallback =
        std::function<void(const QVariantList& rows)>;

    explicit MdbListService(MdbListSettings* settings,
                            QObject* parent = nullptr);

    [[nodiscard]] QVariantList ratings() const { return m_ratings; }
    void clearCache();

    /// Resolves display rows for a meta identity (gate + cache + fetch).
    /// Callback always fires (possibly empty); the ratings property
    /// updates only for the latest requested key (stale-guard).
    void ratingsFor(const QString& metaType, const QString& metaId,
                    RatingsCallback done = {});
    /// QML entry: fetches for the given identity, publishing ratings.
    Q_INVOKABLE void fetchRatings(const QString& metaType,
                                  const QString& metaId);

    /// Test seam: base url override (default api.mdblist.com).
    void setBaseUrl(const QString& base) { m_base = base; }

signals:
    void ratingsChanged();

private:
    void publish(const QString& key, const QVariantList& rows);

    MdbListSettings* m_settings = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QString m_base = QStringLiteral("https://api.mdblist.com");
    QHash<QString, QVariantList> m_cache;   // raw {source,value} rows
    QVariantList m_ratings;
    QString m_latestKey;
    quint64 m_token = 0;
};

} // namespace nuvio::mdblist
