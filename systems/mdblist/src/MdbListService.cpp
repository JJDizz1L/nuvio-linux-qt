#include "nuvio/mdblist/MdbListService.h"

#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrl>

#include "nuvio/mdblist/MdbListSettings.h"

namespace nuvio::mdblist {

const QStringList kProviderPriority{
    QStringLiteral("imdb"),      QStringLiteral("tmdb"),
    QStringLiteral("tomatoes"),  QStringLiteral("metacritic"),
    QStringLiteral("trakt"),     QStringLiteral("letterboxd"),
    QStringLiteral("audience"),  QStringLiteral("mal"),
};

const QList<RatingDisplay> kRatingDisplayOrder{
    {QStringLiteral("imdb"), QStringLiteral("IMDb"), QStringLiteral("#F5C518"),
     RatingDisplay::Format::OneDecimal},
    {QStringLiteral("tmdb"), QStringLiteral("TMDB"), QStringLiteral("#01B4E4"),
     RatingDisplay::Format::Whole},
    {QStringLiteral("trakt"), QStringLiteral("Trakt"),
     QStringLiteral("#ED1C24"), RatingDisplay::Format::Whole},
    {QStringLiteral("letterboxd"), QStringLiteral("Letterboxd"),
     QStringLiteral("#00E054"), RatingDisplay::Format::OneDecimal},
    {QStringLiteral("mal"), QStringLiteral("MyAnimeList"),
     QStringLiteral("#2E51A2"), RatingDisplay::Format::OneDecimal},
    {QStringLiteral("tomatoes"), QStringLiteral("Rotten Tomatoes"),
     QStringLiteral("#FA320A"), RatingDisplay::Format::Percent},
    {QStringLiteral("audience"), QStringLiteral("Audience Score"),
     QStringLiteral("#FA320A"), RatingDisplay::Format::Percent},
    {QStringLiteral("metacritic"), QStringLiteral("Metacritic"),
     QStringLiteral("#FFCC33"), RatingDisplay::Format::Whole},
};

QString extractImdbId(const QString& value)
{
    static const QRegularExpression kImdb(QStringLiteral("tt\\d+"));
    const QRegularExpressionMatch m = kImdb.match(value);
    return m.hasMatch() ? m.captured(0) : QString();
}

QString toMdbListMediaType(const QString& metaType)
{
    return metaType.trimmed().compare(QLatin1String("movie"),
                                      Qt::CaseInsensitive) == 0
               ? QStringLiteral("movie")
               : QStringLiteral("show");
}

QString ratingUrl(const QString& base, const QString& mediaType,
                  const QString& providerId, const QString& apiKey)
{
    QString b = base;
    while (b.endsWith(u'/')) b.chop(1);
    return b + QStringLiteral("/rating/") + mediaType + u'/' + providerId +
           QStringLiteral("?apikey=") +
           QString::fromUtf8(QUrl::toPercentEncoding(apiKey));
}

QByteArray ratingRequestBody(const QString& imdbId)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("ids"),
         QJsonArray{imdbId}},
        {QStringLiteral("provider"), QStringLiteral("imdb")},
    }).toJson(QJsonDocument::Compact);
}

std::optional<double> parseRating(const QByteArray& body)
{
    const QJsonArray arr = QJsonDocument::fromJson(body)
                               .object()
                               .value(QStringLiteral("ratings"))
                               .toArray();
    if (arr.isEmpty()) return std::nullopt;
    const QJsonValue rating =
        arr.first().toObject().value(QStringLiteral("rating"));
    if (rating.isNull() || rating.isUndefined()) return std::nullopt;
    return rating.toDouble();
}

QString formatOneDecimal(double value)
{
    // Kotlin (value*10).roundToInt() rounds half away from zero.
    const long rounded = std::lround(value * 10.0);
    const long whole = rounded / 10;
    long decimal = rounded % 10;
    if (decimal < 0) decimal = -decimal;
    return QString::number(whole) + u'.' + QString::number(decimal);
}

QString formatWhole(double value)
{
    return QString::number(std::lround(value));
}

QString formatPercent(double value)
{
    return QString::number(std::lround(value)) + u'%';
}

MdbListService::MdbListService(MdbListSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    Q_ASSERT(m_settings);
    if (const QByteArray env = qgetenv("NUVIO_MDBLIST_BASE");
        !env.isEmpty())
        m_base = QString::fromUtf8(env);
    m_nam = new QNetworkAccessManager(this);
    // Superset invalidation (fork clears only on key/provider edits;
    // any settings change rebuilding is equally correct, just earlier).
    connect(m_settings, &MdbListSettings::changed, this,
            &MdbListService::clearCache);
}

void MdbListService::clearCache() { m_cache.clear(); }

void MdbListService::ratingsFor(const QString& metaType,
                                const QString& metaId, RatingsCallback done)
{
    const auto finish = [&](const QVariantList& rows) {
        if (done) done(rows);
    };
    const bool enabled = m_settings->enabled();
    const QString key = m_settings->apiKey().trimmed();
    const QStringList providers = m_settings->enabledProviders();
    const QString imdbId = extractImdbId(metaId);
    // shouldFetchForMeta parity: key + providers + resolvable imdb id.
    if (!enabled || key.isEmpty() || providers.isEmpty() ||
        imdbId.isEmpty()) {
        finish({});
        return;
    }
    const QString media = toMdbListMediaType(metaType);
    // Cache key binds the key + provider set (fork parity).
    const QString cacheKey =
        media + u':' + imdbId + u':' + key + u':' + providers.join(u',');
    if (m_cache.contains(cacheKey)) {
        finish(m_cache.value(cacheKey));
        return;
    }
    const quint64 token = ++m_token;
    auto pending = std::make_shared<int>(providers.size());
    auto raws = std::make_shared<QVariantList>();
    for (const QString& provider : providers) {
        QNetworkRequest req{QUrl(ratingUrl(m_base, media, provider, key))};
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/json");
        QNetworkReply* rep = m_nam->post(req, ratingRequestBody(imdbId));
        connect(rep, &QNetworkReply::finished, this,
                [this, rep, provider, cacheKey, token, pending, raws,
                 finish] {
                    rep->deleteLater();
                    if (rep->error() == QNetworkReply::NoError) {
                        if (const auto rating =
                                parseRating(rep->readAll())) {
                            raws->append(QVariantMap{
                                {QStringLiteral("source"), provider},
                                {QStringLiteral("value"), *rating},
                            });
                        }
                    }
                    if (--(*pending) > 0) return;
                    // Priority order (fork: filterNotNull preserves the
                    // providers mapping order).
                    QVariantList ordered;
                    for (const QString& id : kProviderPriority) {
                        for (const QVariant& r : *raws) {
                            if (r.toMap().value("source").toString() == id)
                                ordered.append(r);
                        }
                    }
                    m_cache.insert(cacheKey, ordered);
                    if (token == m_token) finish(ordered);
                });
    }
}

void MdbListService::fetchRatings(const QString& metaType,
                                  const QString& metaId)
{
    const QString key =
        metaType.trimmed() + u'|' + extractImdbId(metaId);
    m_latestKey = key;
    ratingsFor(metaType, metaId, [this, key](const QVariantList& rows) {
        if (key != m_latestKey) return;   // stale-guard
        publish(key, rows);
    });
}

void MdbListService::publish(const QString&, const QVariantList& rows)
{
    // Display rows in fork display order with verbatim formats.
    QVariantList out;
    for (const RatingDisplay& d : kRatingDisplayOrder) {
        for (const QVariant& r : rows) {
            const QVariantMap m = r.toMap();
            if (m.value("source").toString() != d.source) continue;
            const double value = m.value("value").toDouble();
            QString text;
            switch (d.format) {
            case RatingDisplay::Format::OneDecimal:
                text = formatOneDecimal(value);
                break;
            case RatingDisplay::Format::Whole:
                text = formatWhole(value);
                break;
            case RatingDisplay::Format::Percent:
                text = formatPercent(value);
                break;
            }
            out.append(QVariantMap{
                {QStringLiteral("source"), d.source},
                {QStringLiteral("label"), d.label},
                {QStringLiteral("text"), text},
                {QStringLiteral("color"), d.color},
            });
        }
    }
    m_ratings = out;
    emit ratingsChanged();
}

} // namespace nuvio::mdblist
