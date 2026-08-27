#include "nuvio/library/CatalogService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <cstdio>
#include <utility>

namespace nuvio::library {
namespace {

constexpr auto kDefaultBase = "https://v3-cinemeta.strem.io";
constexpr auto kMetahubBase = "https://images.metahub.space/poster/medium";

} // namespace

CatalogService::CatalogService(QObject* parent)
    : QObject(parent),
      m_baseUrl(qgetenv("NUVIO_CINEMETA_BASE")),
      m_nam(new QNetworkAccessManager(this))
{
    if (m_baseUrl.isEmpty()) m_baseUrl = kDefaultBase;
}

QVariantMap CatalogService::itemFromMeta(const QVariantMap& meta,
                                         const QByteArray& baseUrl)
{
    QVariantMap out;
    const QString id = meta.value(QStringLiteral("id")).toString();
    if (!id.startsWith(QLatin1String("tt"))) return {};   // imdb-only for now

    QString poster = meta.value(QStringLiteral("poster")).toString();
    if (poster.isEmpty() || !poster.startsWith(QLatin1String("http"))) {
        poster = QString::fromUtf8(baseUrl) + QLatin1Char('/') + id +
                 QStringLiteral("/img");
    }

    out.insert(QStringLiteral("id"),     id);
    out.insert(QStringLiteral("name"),
               meta.value(QStringLiteral("name")).toString());
    out.insert(QStringLiteral("year"),
               meta.value(QStringLiteral("releaseInfo")).toString());
    // metahub canonical form: {base}/{size}/{id}/img - normalize any other
    // slash-style the addon hands us.
    poster.replace(QLatin1String("//poster/"), QLatin1String("/poster/"));
    out.insert(QStringLiteral("poster"), poster);
    return out;
}

void CatalogService::fetch(const QString& type, const QString& catalogId)
{
    const QUrl url(QString::fromUtf8(m_baseUrl) + QStringLiteral("/catalog/") +
                   type + QLatin1Char('/') + catalogId +
                   QStringLiteral(".json"));
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/json");
    auto* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, type, catalogId] {
        rep->deleteLater();
        if (rep->error() != QNetworkReply::NoError) {
            m_lastError = rep->errorString();
            ingest(type, catalogId, QByteArrayLiteral("{}"));
            return;
        }
        ingest(type, catalogId, rep->readAll());
    });
}

QString CatalogService::shelfTitle(const QString& type,
                                   const QString& catalogId)
{
    const QString key = type + QLatin1Char('/') + catalogId;
    if (key == QLatin1String("movie/top"))  return QStringLiteral("Popular Movies");
    if (key == QLatin1String("series/top")) return QStringLiteral("Popular Series");
    if (key == QLatin1String("anime/top"))  return QStringLiteral("Anime");
    return type + QLatin1Char(' ') + catalogId;
}

QVariantMap CatalogService::newShelf(const QString& type,
                                     const QString& catalogId)
{
    QVariantMap s;
    s.insert(QStringLiteral("type"),      type);
    s.insert(QStringLiteral("catalogId"), catalogId);
    s.insert(QStringLiteral("title"),     shelfTitle(type, catalogId));
    s.insert(QStringLiteral("items"),     QVariantList{});
    s.insert(QStringLiteral("loading"),   true);
    return s;
}

int CatalogService::indexOfShelf(const QString& type,
                                 const QString& catalogId) const
{
    for (int i = 0; i < m_shelves.size(); ++i) {
        const auto s = m_shelves[i].toMap();
        if (s.value("type") == type && s.value("catalogId") == catalogId)
            return i;
    }
    return -1;
}

void CatalogService::loadShelves()
{
    m_shelves.clear();
    m_order.clear();
    using P = std::pair<QString, QString>;
    const P defs[] = {{QStringLiteral("movie"),  QStringLiteral("top")},
                      {QStringLiteral("series"), QStringLiteral("top")},
                      {QStringLiteral("anime"),  QStringLiteral("top")}};
    for (const auto& [t, cid] : defs) {
        m_order << t + QLatin1Char('/') + cid;
        m_shelves.append(newShelf(t, cid));
        fetch(t, cid);          // concurrent; replies funnel through ingest
    }
    emit shelvesChanged();
}

void CatalogService::ingest(const QString& type, const QString& catalogId,
                            const QByteArray& body)
{
    QVariantList items;
    int dropped = 0;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray metas = doc.object().value(QLatin1String("metas")).toArray();
    for (const auto& v : metas) {
        const QVariantMap item = itemFromMeta(v.toObject().toVariantMap(),
                                              m_baseUrl);
        if (item.isEmpty()) { ++dropped; continue; }
        items.append(item);
    }
    if (dropped > 0)
        std::fprintf(stderr,
                     "catalog: dropped %d malformed entries (%s/%s)\n",
                     dropped, qPrintable(type), qPrintable(catalogId));
    m_lastError.clear();

    // Fold into the shelf container (ad-hoc fetches append a shelf row too
    // so single-shelf consumers keep working).
    int idx = indexOfShelf(type, catalogId);
    if (idx < 0) {
        m_order << type + QLatin1Char('/') + catalogId;
        m_shelves.append(newShelf(type, catalogId));
        idx = int(m_shelves.size()) - 1;
    }
    auto shelf = m_shelves[idx].toMap();
    shelf[QLatin1String("items")]   = items;
    shelf[QLatin1String("loading")] = false;
    m_shelves[idx] = shelf;
    emit shelvesChanged();
}

void CatalogService::handleReply(const QString& type,
                                 const QString& catalogId,
                                 const QByteArray& body)
{
    ingest(type, catalogId, body);   // shared parse path (test-driven)
}

} // namespace nuvio::library