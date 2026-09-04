#include "nuvio/tmdb/TmdbService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>

#include "nuvio/tmdb/TmdbSettings.h"

namespace nuvio::tmdb {

QString normalizeMediaType(const QString& mediaType)
{
    const QString t = mediaType.trimmed().toLower();
    if (t == QLatin1String("movie") || t == QLatin1String("film"))
        return QStringLiteral("movie");
    if (t == QLatin1String("tv") || t == QLatin1String("series") ||
        t == QLatin1String("show") || t == QLatin1String("tvshow"))
        return QStringLiteral("tv");
    return t;
}

QString normalizeVideoId(const QString& videoId)
{
    QString v = videoId.trimmed();
    // Sequential single-strip chain (Kotlin removePrefix parity: each
    // prefix strips at most once, in order).
    for (const QLatin1String prefix :
         {QLatin1String("tmdb:"), QLatin1String("movie:"),
          QLatin1String("series:")}) {
        if (v.startsWith(prefix, Qt::CaseInsensitive))
            v = v.mid(prefix.size());
    }
    // Composite tails ("tt123:1:2") and path tails resolve to the head.
    const int colon = v.indexOf(u':');
    if (colon >= 0) v = v.left(colon);
    const int slash = v.indexOf(u'/');
    if (slash >= 0) v = v.left(slash);
    return v.trimmed();
}

QString buildTmdbUrl(const QString& base, const QString& endpoint,
                     const QString& apiKey,
                     const QList<QPair<QString, QString>>& query)
{
    QString url = base;
    while (url.endsWith(u'/')) url.chop(1);
    QString ep = endpoint;
    while (ep.startsWith(u'/')) ep = ep.mid(1);
    url += u'/' + ep + QStringLiteral("?api_key=") +
           QString::fromUtf8(QUrl::toPercentEncoding(apiKey));
    for (const auto& [key, value] : query) {
        if (value.trimmed().isEmpty()) continue;   // blank values dropped
        url += u'&' + QString::fromUtf8(QUrl::toPercentEncoding(key)) +
               u'=' + QString::fromUtf8(QUrl::toPercentEncoding(value));
    }
    return url;
}

QString parseFindResult(const QByteArray& body,
                        const QString& normalizedType)
{
    const QJsonObject root =
        QJsonDocument::fromJson(body).object();
    const auto firstId = [](const QJsonValue& v) -> int {
        const QJsonArray arr = v.toArray();
        if (arr.isEmpty()) return -1;
        return arr.first().toObject().value(QStringLiteral("id")).toInt(-1);
    };
    int id = -1;
    if (normalizedType == QLatin1String("movie")) {
        id = firstId(root.value(QStringLiteral("movie_results")));
    } else if (normalizedType == QLatin1String("tv")) {
        id = firstId(root.value(QStringLiteral("tv_results")));
    } else {
        id = firstId(root.value(QStringLiteral("movie_results")));
        if (id <= 0)
            id = firstId(root.value(QStringLiteral("tv_results")));
    }
    return id > 0 ? QString::number(id) : QString();
}

QString parseExternalIds(const QByteArray& body)
{
    return QJsonDocument::fromJson(body)
        .object()
        .value(QStringLiteral("imdb_id"))
        .toString()
        .trimmed();
}

TmdbService::TmdbService(TmdbSettings* settings, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    Q_ASSERT(m_settings);
    if (const QByteArray env = qgetenv("NUVIO_TMDB_BASE"); !env.isEmpty())
        m_base = QString::fromUtf8(env);
    m_nam = new QNetworkAccessManager(this);
}

void TmdbService::fetch(
    const QString& endpoint,
    const QList<QPair<QString, QString>>& query,
    std::function<void(const QByteArray&)> done)
{
    const QString key = m_settings->apiKey().trimmed();
    if (key.isEmpty()) {
        done({});
        return;
    }
    QNetworkReply* rep =
        m_nam->get(QNetworkRequest{QUrl(buildTmdbUrl(m_base, endpoint, key,
                                                     query))});
    connect(rep, &QNetworkReply::finished, this,
            [rep, done = std::move(done)] {
                rep->deleteLater();
                if (rep->error() != QNetworkReply::NoError) {
                    done({});
                    return;
                }
                done(rep->readAll());
            });
}

void TmdbService::ensureTmdbId(const QString& videoId,
                               const QString& mediaType, IdCallback done)
{
    const QString normalized = normalizeVideoId(videoId);
    if (normalized.isEmpty() || !m_settings->hasApiKey()) {
        done({});
        return;
    }
    bool allDigits = !normalized.isEmpty();
    for (const QChar c : normalized) {
        if (!c.isDigit()) {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        done(normalized);   // numeric ids pass through (no network)
        return;
    }
    if (!normalized.startsWith(QLatin1String("tt"), Qt::CaseInsensitive)) {
        done({});
        return;
    }
    const QString type = normalizeMediaType(mediaType);
    const QString cacheKey = normalized + u':' + type;
    if (m_imdbToTmdb.contains(cacheKey)) {
        done(m_imdbToTmdb.value(cacheKey));
        return;
    }
    fetch(QStringLiteral("find/") + normalized,
          {{QStringLiteral("external_source"), QStringLiteral("imdb_id")}},
          [this, done = std::move(done), cacheKey, normalized,
           type](const QByteArray& body) mutable {
              const QString id =
                  body.isEmpty() ? QString() : parseFindResult(body, type);
              if (!id.isEmpty()) {
                  m_imdbToTmdb.insert(cacheKey, id);
                  m_tmdbToImdb.insert(id + u':' + type, normalized);
              }
              done(id);
          });
}

void TmdbService::tmdbToImdb(int tmdbId, const QString& mediaType,
                             IdCallback done)
{
    if (!m_settings->hasApiKey()) {
        done({});
        return;
    }
    const QString type = normalizeMediaType(mediaType);
    const QString cacheKey =
        QString::number(tmdbId) + u':' + type;
    if (m_tmdbToImdb.contains(cacheKey)) {
        done(m_tmdbToImdb.value(cacheKey));
        return;
    }
    const QString endpoint =
        (type == QLatin1String("tv") ? QStringLiteral("tv/")
                                     : QStringLiteral("movie/")) +
        QString::number(tmdbId) + QStringLiteral("/external_ids");
    fetch(endpoint, {},
          [this, done = std::move(done), cacheKey,
           type](const QByteArray& body) mutable {
              const QString id =
                  body.isEmpty() ? QString() : parseExternalIds(body);
              if (!id.isEmpty()) {
                  m_tmdbToImdb.insert(cacheKey, id);
                  m_imdbToTmdb.insert(id + u':' + type,
                                      cacheKey.section(u':', 0, 0));
              }
              done(id);
          });
}

} // namespace nuvio::tmdb
