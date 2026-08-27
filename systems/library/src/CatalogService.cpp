#include "nuvio/library/CatalogService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

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
            emit catalogReady(type, catalogId, {});
            return;
        }
        handleReply(type, catalogId, rep->readAll());
    });
}

void CatalogService::handleReply(const QString& type,
                                 const QString& catalogId,
                                 const QByteArray& body)
{
    QVariantList items;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray metas = doc.object().value(QLatin1String("metas")).toArray();

    int dropped = 0;
    for (const auto& v : metas) {
        const QVariantMap item =
            itemFromMeta(v.toObject().toVariantMap(), m_baseUrl);
        if (item.isEmpty()) {
            ++dropped;
            continue;
        }
        items.append(item);
    }
    if (dropped > 0)
        std::fprintf(stderr,
                     "catalog: dropped %d malformed entries (%s/%s)\n",
                     dropped, qPrintable(type), qPrintable(catalogId));
    m_lastError.clear();
    emit catalogReady(type, catalogId, items);
}

} // namespace nuvio::library