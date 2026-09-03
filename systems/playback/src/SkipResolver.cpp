#include "nuvio/playback/SkipResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

namespace nuvio::playback {

namespace {
constexpr auto kAniskipBase = "https://api.aniskip.com/v2/";

[[nodiscard]] std::optional<SkipSegment> introDbSegment(
    const QJsonObject& o, const QString& type, const QString& provider)
{
    double start = -1.0, end = -1.0;
    if (o.value(QStringLiteral("start_sec")).isDouble())
        start = o.value(QStringLiteral("start_sec")).toDouble();
    else if (o.value(QStringLiteral("start_ms")).isDouble())
        start = o.value(QStringLiteral("start_ms")).toDouble() / 1000.0;
    if (o.value(QStringLiteral("end_sec")).isDouble())
        end = o.value(QStringLiteral("end_sec")).toDouble();
    else if (o.value(QStringLiteral("end_ms")).isDouble())
        end = o.value(QStringLiteral("end_ms")).toDouble() / 1000.0;
    if (start < 0.0 || end < 0.0 || end <= start) return std::nullopt;
    return SkipSegment{start, end, type, provider};
}

[[nodiscard]] std::optional<QString> segmentCategory(const QString& type)
{
    const QString t = type.toLower();
    if (t == QLatin1String("intro") || t == QLatin1String("op") ||
        t == QLatin1String("mixed-op"))
        return QStringLiteral("opening");
    if (t == QLatin1String("outro") || t == QLatin1String("ed") ||
        t == QLatin1String("mixed-ed") || t == QLatin1String("credits") ||
        t == QLatin1String("ending"))
        return QStringLiteral("ending");
    if (t == QLatin1String("recap")) return QStringLiteral("recap");
    return std::nullopt;
}

[[nodiscard]] QVariantList toVariantList(const QList<SkipSegment>& segs)
{
    QVariantList out;
    for (const auto& s : segs)
        out.append(QVariantMap{
            {QStringLiteral("startSec"), s.startSec},
            {QStringLiteral("endSec"), s.endSec},
            {QStringLiteral("type"), s.type},
            {QStringLiteral("provider"), s.provider},
        });
    return out;
}
} // namespace

QString introDbSegmentsUrl(const QString& baseUrl, const QString& imdbId,
                           int season, int episode)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(u'/')) base.chop(1);   // Compose trimEnd('/')
    if (base.isEmpty()) return {};
    QUrl url(base + QStringLiteral("/segments"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("imdb_id"), imdbId);
    q.addQueryItem(QStringLiteral("season"), QString::number(season));
    q.addQueryItem(QStringLiteral("episode"), QString::number(episode));
    url.setQuery(q);
    return url.toString();
}

QString aniskipUrl(const QString& malId, int episode)
{
    return QString::fromLatin1(kAniskipBase) +
           QStringLiteral("skip-times/") + malId + u'/' +
           QString::number(episode) +
           QStringLiteral("?types=op&types=ed&types=recap&types=mixed-op"
                          "&types=mixed-ed&episodeLength=0");
}

QString introDbSubmitUrl(const QString& baseUrl)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(u'/')) base.chop(1);
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/submit");
}

QByteArray introDbSubmitBody(const QString& imdbId, int season, int episode,
                             double startSec, double endSec)
{
    const QJsonObject o{
        {QStringLiteral("imdb_id"), imdbId},
        {QStringLiteral("season"), season},
        {QStringLiteral("episode"), episode},
        {QStringLiteral("start_sec"), startSec},
        {QStringLiteral("end_sec"), endSec},
    };
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QList<SkipSegment> parseIntroDbSegments(const QByteArray& body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    QList<SkipSegment> out;
    const struct {
        const char* key;
        const char* type;
    } legs[] = {{"intro", "intro"}, {"recap", "recap"}, {"outro", "outro"}};
    for (const auto& leg : legs) {
        const auto seg = introDbSegment(
            root.value(QLatin1String(leg.key)).toObject(),
            QLatin1String(leg.type), QStringLiteral("introdb"));
        if (seg) out.append(*seg);
    }
    return out;
}

QList<SkipSegment> parseAniSkipTimes(const QByteArray& body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (!root.value(QStringLiteral("found")).toBool()) return {};
    QList<SkipSegment> out;
    for (const QJsonValue& r : root.value(QStringLiteral("results")).toArray()) {
        const QJsonObject o = r.toObject();
        const QJsonObject iv = o.value(QStringLiteral("interval")).toObject();
        if (!iv.value(QStringLiteral("startTime")).isDouble() ||
            !iv.value(QStringLiteral("endTime")).isDouble())
            continue;
        const double start = iv.value(QStringLiteral("startTime")).toDouble();
        const double end = iv.value(QStringLiteral("endTime")).toDouble();
        if (end <= start) continue;
        out.append({start, end,
                    o.value(QStringLiteral("skipType")).toString(),
                    QStringLiteral("aniskip")});
    }
    return out;
}

QList<SkipSegment> mergeSkipIntervals(
    const QList<QList<SkipSegment>>& providerResults)
{
    QList<SkipSegment> out;
    QSet<QString> taken;
    for (const auto& list : providerResults) {
        for (const auto& s : list) {
            const auto cat = segmentCategory(s.type);
            if (!cat || taken.contains(*cat)) continue;
            taken.insert(*cat);
            out.append(s);
        }
    }
    return out;
}

QString skipCompletionKey(const QString& provider, const QString& type,
                          double startSec, double endSec)
{
    return provider + u':' + type + u':' + QString::number(startSec) + u':' +
           QString::number(endSec);
}

SkipResolver::SkipResolver(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{}

void SkipResolver::setProviders(Providers providers)
{
    m_providers = std::move(providers);
}

void SkipResolver::resolve(const QString& id, int season, int episode)
{
    const quint64 token = ++m_token;
    const QString norm = id.trimmed();
    QString key;
    QString introdbUrl;
    QString aniskip;
    QString submitImdb;
    int submitS = -1, submitE = -1;

    if (norm.startsWith(QLatin1String("mal:"), Qt::CaseInsensitive)) {
        const QStringList parts = norm.split(u':');
        const QString malId = parts.value(1).trimmed();
        bool okEp = false;
        const int embeddedEp = parts.value(2).toInt(&okEp);
        const int ep = episode >= 0 ? episode : (okEp ? embeddedEp : -1);
        if (malId.isEmpty() || ep < 0) {
            emit intervals({});
            return;
        }
        key = "mal:" + malId + u':' + QString::number(ep);
        if (m_cache.contains(key)) {
            emit intervals(toVariantList(m_cache.value(key)));
            return;
        }
        aniskip = aniskipUrl(malId, ep);
    } else if (const auto tt = extractImdbId(norm)) {
        const CompositeId parts = splitCompositeId(norm);
        const int s = season >= 0 ? season : parts.season;
        const int e = episode >= 0 ? episode : parts.episode;
        if (s < 0 || e < 0) {
            emit intervals({});
            return;
        }
        key = *tt + u':' + QString::number(s) + u':' + QString::number(e);
        if (m_cache.contains(key)) {
            emit intervals(toVariantList(m_cache.value(key)));
            return;
        }
        submitImdb = *tt;
        submitS = s;
        submitE = e;
        if (m_providers.skipIntroEnabled())
            introdbUrl = introDbSegmentsUrl(m_providers.introDbBaseUrl(), *tt,
                                            s, e);
    } else {
        // kitsu: and unknown schemes need Simkl cross-id resolution
        // (tracking backlog) - honest empty.
        emit intervals({});
        return;
    }

    if (introdbUrl.isEmpty() && aniskip.isEmpty()) {
        m_cache.insert(key, {});
        emit intervals({});
        return;
    }

    m_key = key;
    m_pending.clear();
    m_awaiting = 0;
    m_submitImdb = submitImdb;
    m_submitSeason = submitS;
    m_submitEpisode = submitE;

    auto fetch = [this, token](const QString& url) {
        ++m_awaiting;
        QNetworkReply* rep = m_nam->get(QNetworkRequest(QUrl(url)));
        connect(rep, &QNetworkReply::finished, this, [this, rep, token, url] {
            rep->deleteLater();
            if (token != m_token) return;   // superseded lookup
            QList<SkipSegment> segs;
            if (rep->error() == QNetworkReply::NoError) {
                const QByteArray body = rep->readAll();
                segs = url.contains(QLatin1String("aniskip"))
                           ? parseAniSkipTimes(body)
                           : parseIntroDbSegments(body);
            }
            ingest(segs);
        });
    };
    if (!introdbUrl.isEmpty()) fetch(introdbUrl);
    if (!aniskip.isEmpty()) fetch(aniskip);

    // Overall guard: emit whatever arrived (partial merge allowed).
    QTimer::singleShot(15000, this, [this, token] {
        if (token != m_token || m_awaiting <= 0) return;
        m_awaiting = 0;
        m_cache.insert(m_key, mergeSkipIntervals(m_pending));
        emit intervals(toVariantList(m_cache.value(m_key)));
    });
}

void SkipResolver::ingest(QList<SkipSegment> segs)
{
    m_pending.append(std::move(segs));
    if (--m_awaiting > 0) return;
    m_cache.insert(m_key, mergeSkipIntervals(m_pending));
    emit intervals(toVariantList(m_cache.value(m_key)));
}

void SkipResolver::submit(double startSec, double endSec)
{
    if (!m_providers.introSubmitEnabled() || m_submitImdb.isEmpty() ||
        m_submitSeason < 0 || m_submitEpisode < 0 ||
        endSec <= startSec) {
        emit submitted(false);
        return;
    }
    const QString apiKey = m_providers.introDbApiKey();
    const QString url = introDbSubmitUrl(m_providers.introDbBaseUrl());
    if (apiKey.trimmed().isEmpty() || url.isEmpty()) {
        emit submitted(false);
        return;
    }
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + apiKey.trimmed().toUtf8());
    QNetworkReply* rep = m_nam->post(
        req, introDbSubmitBody(m_submitImdb, m_submitSeason, m_submitEpisode,
                               startSec, endSec));
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        const int status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                               .toInt();
        emit submitted(status == 200 || status == 201);
    });
}

} // namespace nuvio::playback
