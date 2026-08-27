#include "nuvio/library/AddonRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <nuvio/settings/PropertiesStore.h>

namespace nuvio::library {

AddonRegistry::AddonRegistry(QObject* parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this))
{
}

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

void AddonRegistry::load()
{
    m_addons.clear();
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("qt-addons"));
    for (int i = 0; i < 512; ++i) {
        const auto raw = store.getString(
            QStringLiteral("addon_%1").arg(i).toStdString());
        if (!raw) continue;
        const auto entry =
            QJsonDocument::fromJson(QByteArray::fromStdString(*raw))
                .object()
                .toVariantMap();
        if (!entry.isEmpty()) m_addons.append(entry);
    }
    emit changed();
}

void AddonRegistry::add(const QString& manifestUrlIn)
{
    QString url = manifestUrlIn.trimmed();
    if (url.isEmpty()) { emit addResult(false, "URL required"); return; }
    if (!url.startsWith(QLatin1String("http"))) {
        url = QStringLiteral("https://") + url;
    }
    if (!url.contains(QLatin1String("manifest.json"))) {
        while (url.endsWith(QLatin1Char('/'))) url.chop(1);
        url += QStringLiteral("/manifest.json");
    }
    // dedupe by URL
    for (const auto& a : m_addons)
        if (a.toMap().value("url").toString().compare(url,
                Qt::CaseInsensitive) == 0) {
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
        finishAdd(url, rep->readAll());
    });
}

void AddonRegistry::finishAdd(const QString& normalizedUrl,
                              const QByteArray& body)
{
    const QVariantMap manifest = parseManifest(normalizedUrl, body);
    if (manifest.isEmpty()) {
        emit addResult(false, "Not a valid Stremio manifest");
        return;
    }
    // dedupe by id too (same addon via alternate mirror URL)
    const QString newId = manifest.value("id").toString();
    for (const auto& a : m_addons)
        if (a.toMap().value("id").toString() == newId) {
            emit addResult(false, "Already installed (mirror)");
            return;
        }

    m_addons.append(manifest);
    persist();
    emit changed();
    emit addResult(true,
                   QStringLiteral("Installed: %1")
                       .arg(manifest.value("name").toString()));
}

void AddonRegistry::remove(const QString& id)
{
    for (int i = 0; i < m_addons.size(); ++i) {
        if (m_addons[i].toMap().value("id").toString() == id) {
            m_addons.removeAt(i);
            persist();
            emit changed();
            emit removed(id);
            return;
        }
    }
}

void AddonRegistry::persist()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("qt-addons"));
    for (int i = 0; i < 256; ++i)   // clear previous high-water marks
        store.remove(QStringLiteral("addon_%1").arg(i).toStdString());
    int n = 0;
    for (const auto& a : m_addons) {
        const QByteArray j = QJsonDocument::fromVariant(a)
                                 .toJson(QJsonDocument::Compact);
        store.putString(QStringLiteral("addon_%1").arg(n).toStdString(),
                        std::string(j.constData(), size_t(j.size())));
        ++n;
    }
}

} // namespace nuvio::library