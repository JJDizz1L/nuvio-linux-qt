#include "nuvio/library/MetaService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <cstdio>

namespace nuvio::library {

namespace {
constexpr auto kDefaultBase     = "https://v3-cinemeta.strem.io";
constexpr auto kMetahubPoster   = "https://images.metahub.space/poster/medium";
constexpr auto kMetahubBackgrnd = "https://images.metahub.space/background/medium";

// Cinemeta video ids: modern "tt123:1:4", legacy "tt123::season:1:episode:4".
const QRegularExpression kModernIdPattern(QStringLiteral(":([0-9]+):([0-9]+)$"));
const QRegularExpression kLegacyIdPattern(
    QStringLiteral("::season:([0-9]+):episode:([0-9]+)$"));

QStringList stringListOr(const QJsonValue& v)
{
    QStringList out;
    if (v.isArray())
        for (const auto& e : v.toArray()) {
            const QJsonObject o = e.toObject();
            out.append(o.isEmpty() ? e.toString()
                                   : o.value(QLatin1String("name")).toString());
        }
    else if (v.isString())
        out.append(v.toString());
    return out;
}

QVariantMap normalizeVideo(const QJsonObject& v)
{
    QVariantMap out;
    bool okSeason = false, okEpisode = false;
    int season = 0, episode = 0;
    const QJsonValue sj = v.value(QLatin1String("season"));
    const QJsonValue ej = v.value(QLatin1String("episode"));
    if (sj.isDouble()) { season = sj.toInt(); okSeason = true; }
    if (ej.isDouble()) { episode = ej.toInt(); okEpisode = true; }
    if (!okSeason || !okEpisode) {          // fall back to id parsing
        const QString id = v.value(QLatin1String("id")).toString();
        auto modern = kModernIdPattern.match(id);
        if (modern.hasMatch()) {
            season  = modern.captured(1).toInt();
            episode = modern.captured(2).toInt();
            okSeason = okEpisode = true;
        } else {
            auto legacy = kLegacyIdPattern.match(id);
            if (legacy.hasMatch()) {
                season  = legacy.captured(1).toInt();
                episode = legacy.captured(2).toInt();
                okSeason = okEpisode = true;
            }
        }
    }
    if (!okSeason || !okEpisode) return {}; // malformed entry: dropped

    out.insert(QStringLiteral("season"),      season);
    out.insert(QStringLiteral("episode"),     episode);
    out.insert(QStringLiteral("name"),
               v.value(QLatin1String("name")).toString());
    out.insert(QStringLiteral("description"),
               v.value(QLatin1String("overview")).toString());
    out.insert(QStringLiteral("thumb"),
               v.value(QLatin1String("thumbnail")).toString());
    return out;
}
} // namespace

MetaService::MetaService(QObject* parent)
    : QObject(parent),
      m_baseUrl(qgetenv("NUVIO_CINEMETA_BASE")),
      m_nam(new QNetworkAccessManager(this))
{
    if (m_baseUrl.isEmpty()) m_baseUrl = kDefaultBase;
}

void MetaService::setLoading(bool v)
{
    if (m_loading == v) return;
    m_loading = v;
    emit loadingChanged();
}

void MetaService::publish(const QVariantMap& map)
{
    m_current = map;
    emit currentChanged();
}

QVariantMap MetaService::metaFromJson(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonObject meta =
        doc.object().value(QLatin1String("meta")).toObject();
    if (meta.isEmpty()) return {};

    QVariantMap out;
    const QString id = meta.value(QLatin1String("id")).toString();
    if (!id.startsWith(QLatin1String("tt"))) return {};  // imdb-only, parity
    out.insert(QStringLiteral("id"),          id);
    out.insert(QStringLiteral("type"),
               meta.value(QLatin1String("type")).toString());
    out.insert(QStringLiteral("name"),
               meta.value(QLatin1String("name")).toString());
    out.insert(QStringLiteral("description"),
               meta.value(QLatin1String("description")).toString());
    out.insert(QStringLiteral("releaseInfo"),
               meta.value(QLatin1String("releaseInfo")).toString());
    out.insert(QStringLiteral("runtime"),
               meta.value(QLatin1String("runtime")).toString());

    const QJsonValue rating = meta.value(QLatin1String("imdbRating"));
    out.insert(QStringLiteral("imdbRating"),
               rating.toString());                    // may be absent -> ""

    QString poster = meta.value(QLatin1String("poster")).toString();
    if (poster.isEmpty() || !poster.startsWith(QLatin1String("http")))
        poster = QStringLiteral("%1/%2/img").arg(kMetahubPoster, id);
    out.insert(QStringLiteral("poster"), poster);

    QString bg = meta.value(QLatin1String("background")).toString();
    if (bg.isEmpty() && meta.value(QLatin1String("background")).isArray()
        && meta.value(QLatin1String("background")).toArray().size() > 0)
        bg = meta.value(QLatin1String("background"))
                 .toArray().first().toString();
    if (bg.isEmpty() || !bg.startsWith(QLatin1String("http")))
        bg = QStringLiteral("%1/%2/img").arg(kMetahubBackgrnd, id);
    out.insert(QStringLiteral("background"), bg);

    out.insert(QStringLiteral("genres"),
               stringListOr(meta.value(QLatin1String("genres"))));
    out.insert(QStringLiteral("cast"),
               stringListOr(meta.value(QLatin1String("cast"))));

    // Trailers: [{source:"youtube", key:"..."}] normalized; other
    // providers pass through untouched so the UI can filter.
    QVariantList trailers;
    for (const auto& v :
         meta.value(QLatin1String("trailers")).toArray()) {
        const QJsonObject t = v.toObject();
        QVariantMap e;
        e.insert(QStringLiteral("provider"),
                 t.value(QLatin1String("source")).toString());
        e.insert(QStringLiteral("key"),
                 t.contains(QLatin1String("key"))
                     ? t.value(QLatin1String("key")).toString()
                     : t.value(QLatin1String("id")).toString());
        if (!e.value(QStringLiteral("key")).toString().isEmpty())
            trailers.append(e);
    }
    out.insert(QStringLiteral("trailers"), trailers);

    QList<QVariantMap> videos;
    for (const auto& v :
         meta.value(QLatin1String("videos")).toArray()) {
        QVariantMap ep = normalizeVideo(v.toObject());
        if (ep.isEmpty()) continue;       // dropped + counted below via size
        videos.append(ep);
    }
    std::sort(videos.begin(), videos.end(),
              [](const QVariantMap& a, const QVariantMap& b) {
                  const auto ka = qMakePair(
                      a.value(QLatin1String("season")).toInt(),
                      a.value(QLatin1String("episode")).toInt());
                  const auto kb = qMakePair(
                      b.value(QLatin1String("season")).toInt(),
                      b.value(QLatin1String("episode")).toInt());
                  return ka < kb;
              });
    QVariantList videoList;
    for (const auto& v : videos) videoList.append(v);
    out.insert(QStringLiteral("videos"), videoList);

    return out;
}

void MetaService::load(const QString& type, const QString& imdbId,
                       const QString& displayName)
{
    const QString key = type + QLatin1Char('/') + imdbId;

    // Seed immediately: the page renders id/title instantly (plus cached
    // rich fields when this card was viewed before) and refreshes in place
    // when the network answer lands.
    QVariantMap seed;
    if (m_loadedKey == key) {
        seed = m_current;                       // re-view of current card
    } else {
        seed.insert(QStringLiteral("id"),     imdbId);
        seed.insert(QStringLiteral("type"),   type);
        seed.insert(QStringLiteral("videos"), QVariantList{});
    }
    if (!displayName.isEmpty())
        seed.insert(QStringLiteral("name"), displayName);
    publish(seed);
    m_loadedKey = key;

    setLoading(true);
    m_lastError.clear();
    emit lastErrorChanged();

    QUrl url(QString::fromUtf8(m_baseUrl) + QStringLiteral("/meta/") + type +
             QLatin1Char('/') + imdbId + QStringLiteral(".json"));
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/json");
    auto* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, key] {
        rep->deleteLater();
        setLoading(false);
        if (rep->error() != QNetworkReply::NoError) {
            // Stale-answer guard: a slower reply for an ABANDONED card must
            // not clobber what the user is looking at now.
            if (key != m_loadedKey) return;
            m_lastError = rep->errorString();
            emit lastErrorChanged();
            return;
        }
        if (key != m_loadedKey) return;
        const QVariantMap parsed = metaFromJson(rep->readAll());
        if (parsed.isEmpty()) {
            m_lastError = QStringLiteral("malformed metadata response");
            emit lastErrorChanged();
            return;
        }
        // Keep caller-seeded name if the wire payload lacks one.
        QVariantMap merged = parsed;
        if (merged.value(QStringLiteral("name")).toString().isEmpty()
            && !m_current.value(QStringLiteral("name")).toString().isEmpty()
            && m_current.value(QStringLiteral("id")).toString()
                   == merged.value(QStringLiteral("id")).toString())
            merged.insert(QStringLiteral("name"),
                          m_current.value(QStringLiteral("name")));
        publish(merged);
    });
}

} // namespace nuvio::library