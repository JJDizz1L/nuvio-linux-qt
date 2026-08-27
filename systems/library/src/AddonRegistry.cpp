#include "nuvio/library/AddonRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <memory>

#include <nuvio/settings/PropertiesStore.h>

namespace nuvio::library {

AddonRegistry::AddonRegistry(QObject* parent)
    : QObject(parent),
      m_truth(std::make_unique<nuvio::settings::PropertiesStore>(
          nuvio::settings::PropertiesStore::defaultPath("addons"))),
      m_cache(std::make_unique<nuvio::settings::PropertiesStore>(
          nuvio::settings::PropertiesStore::defaultPath("qt-addons"))),
      m_nam(new QNetworkAccessManager(this))
{
}

AddonRegistry::~AddonRegistry() = default;

QVariantMap AddonRegistry::parseManifest(const QString& url,
                                         const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonObject obj  = doc.object();
    QVariantMap out;
    out.insert(QStringLiteral("url"), url);
    out.insert(QStringLiteral("id"),
               obj.value(QStringLiteral("id")).toString());
    out.insert(QStringLiteral("name"),
               obj.value(QStringLiteral("name")).toString());
    QStringList types;
    for (const auto& t : obj.value(QLatin1String("types")).toArray())
        types << t.toString();
    out.insert(QStringLiteral("types"), types);
    if (out.value("id").toString().isEmpty() ||
        out.value("name").toString().isEmpty())
        return {};   // not a Stremio manifest
    return out;
}

namespace {
QVariantMap placeholderRow(const QString& url)
{
    return QVariantMap{
        {QStringLiteral("url"),     url},
        {QStringLiteral("id"),      QString()},
        {QStringLiteral("name"),    url},
        {QStringLiteral("types"),   QStringList()},
        {QStringLiteral("enabled"), true},
    };
}
} // namespace

void AddonRegistry::load()
{
    AddonStore::migrateLegacyIndexedEntries(*m_cache);

    m_addons.clear();
    const auto urls = AddonStore::loadInstalledUrls(*m_truth);
    const auto enabled = AddonStore::loadEnabledStates(*m_truth);

    for (const QString& url : urls) {
        const QByteArray cached = AddonStore::loadCachedManifest(*m_cache, url);
        QVariantMap row;
        if (!cached.isEmpty()) {
            row = parseManifest(url, cached);
            if (row.isEmpty()) row = placeholderRow(url);  // stale cache
        } else {
            row = placeholderRow(url);
            fetchManifest(url);                            // async fill-in
        }
        row[QStringLiteral("enabled")] = enabled.value(url, true);
        m_addons.append(row);
    }
    emit changed();
}

void AddonRegistry::rebuildRow(const QString& url, const QByteArray& body)
{
    const QVariantMap manifest = parseManifest(url, body);
    for (int i = 0; i < m_addons.size(); ++i) {
        QVariantMap row = m_addons[i].toMap();
        if (row.value("url").toString() != url) continue;
        if (!manifest.isEmpty()) {
            manifest[QStringLiteral("enabled")] =
                row.value(QStringLiteral("enabled"), true);
            m_addons[i] = manifest;
        }
        break;
    }
    emit changed();
}

void AddonRegistry::fetchManifest(const QString& url)
{
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, url] {
        rep->deleteLater();
        if (rep->error() != QNetworkReply::NoError) return;
        const QByteArray body = rep->readAll();
        AddonStore::saveCachedManifest(*m_cache, url, body);
        m_cache->persist();
        rebuildRow(url, body);
    });
}

void AddonRegistry::add(const QString& manifestUrlIn)
{
    const QString url = AddonStore::normalizeManifestUrl(manifestUrlIn);
    if (url.isEmpty()) { emit addResult(false, "URL required"); return; }

    const QStringList urls = AddonStore::loadInstalledUrls(*m_truth);
    if (urls.contains(url)) {
        emit addResult(false, "Already installed");
        return;
    }

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this, [this, rep, url] {
        rep->deleteLater();
        if (rep->error() != QNetworkReply::NoError) {
            emit addResult(false,
                           QStringLiteral("Fetch failed: %1")
                               .arg(rep->errorString()));
            return;
        }
        const QByteArray body = rep->readAll();
        if (parseManifest(url, body).isEmpty()) {
            emit addResult(false, "Not a valid Stremio manifest");
            return;
        }
        // Install: truth urls + enabled(true) + manifest cache.
        QStringList next = AddonStore::loadInstalledUrls(*m_truth);
        if (!next.contains(url)) next << url;             // race guard
        AddonStore::saveInstalledUrls(*m_truth, next);

        auto enabled = AddonStore::loadEnabledStates(*m_truth);
        enabled.insert(url, true);
        AddonStore::saveEnabledStates(*m_truth, enabled);
        m_truth->persist();

        AddonStore::saveCachedManifest(*m_cache, url, body);
        m_cache->persist();

        QVariantMap row = parseManifest(url, body);
        row.insert(QStringLiteral("enabled"), true);
        m_addons.append(row);

        emit changed();
        emit addResult(true,
                       QStringLiteral("Installed: %1")
                           .arg(row.value("name").toString()));
    });
}

void AddonRegistry::remove(const QString& id)
{
    if (id.isEmpty()) return;
    for (int i = 0; i < m_addons.size(); ++i) {
        const QVariantMap row = m_addons[i].toMap();
        if (row.value("id").toString() != id) continue;
        const QString url = row.value("url").toString();

        m_addons.removeAt(i);

        QStringList urls = AddonStore::loadInstalledUrls(*m_truth);
        urls.removeAll(url);
        AddonStore::saveInstalledUrls(*m_truth, urls);

        auto enabled = AddonStore::loadEnabledStates(*m_truth);
        enabled.remove(url);
        AddonStore::saveEnabledStates(*m_truth, enabled);
        m_truth->persist();

        AddonStore::removeCachedManifest(*m_cache, url);
        m_cache->persist();

        emit changed();
        emit removed(id);
        return;
    }
}

void AddonRegistry::setEnabled(const int index, const bool on)
{
    if (index < 0 || index >= m_addons.size()) return;
    QVariantMap row = m_addons[index].toMap();
    const QString url = row.value("url").toString();
    if (url.isEmpty() || row.value("enabled", true) == on) return;
    row[QStringLiteral("enabled")] = on;
    m_addons[index] = row;

    auto enabled = AddonStore::loadEnabledStates(*m_truth);
    enabled.insert(url, on);
    AddonStore::saveEnabledStates(*m_truth, enabled);
    m_truth->persist();
    emit changed();
}

} // namespace nuvio::library