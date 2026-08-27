#include "nuvio/playback/StreamResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace nuvio::playback {

StreamResolver::StreamResolver(QObject* parent) : QObject(parent) {}

void StreamResolver::setAddons(const QVariantList& addons)
{
    m_addonOrder.clear();
    m_addons.clear();
    for (const auto& v : addons) {
        const auto m = v.toMap();
        AddonDescriptor a;
        a.id          = m.value(QStringLiteral("id")).toString();
        a.name        = m.value(QStringLiteral("name")).toString();
        a.manifestUrl = m.value(QStringLiteral("url")).toString();
        if (!a.valid()) continue;
        m_addons.insert(a.id, a);
        m_addonOrder.append(a.id);
    }
}

// ---- selection policy --------------------------------------------------------

ResolvedStream StreamResolver::fromEntry(const QString& addonId,
                                         const QJsonObject& e)
{
    ResolvedStream s;
    s.source = addonId;
    s.title  = e.value(QLatin1String("title")).toString();

    // Priority inside one entry: explicit url > externalUrl > torrent.
    const QString url =
        e.value(QLatin1String("url")).toString();
    const QString external =
        e.value(QLatin1String("externalUrl")).toString();
    const QString infoHash =
        e.value(QLatin1String("infoHash")).toString();
    if (!url.isEmpty() && url.startsWith(QLatin1String("http")))
        s.url = url;
    else if (!external.isEmpty())
        s.url = external;
    else if (!infoHash.isEmpty())
        s.infoHash = QByteArray::fromHex(infoHash.toLatin1());
    return s;
}

QVariantMap StreamResolver::toVariant(const ResolvedStream& s)
{
    return QVariantMap{{"source",   s.source},
                       {"title",    s.title},
                       {"url",      s.url},
                       {"playable", s.playableDirect()}};
}

void StreamResolver::applyAddonStreams(const QString& key,
                                       const QString& addonId,
                                       const QByteArray& body)
{
    if (addonId.isEmpty()) return;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray streams =
        doc.object().value(QLatin1String("streams")).toArray();

    QList<ResolvedStream> parsed;
    for (const auto& v : streams) {
        const ResolvedStream s = fromEntry(addonId, v.toObject());
        if (s.playableDirect()) {
            parsed.append(s);          // direct source: keep
        }
        // Torrent-only entries are dropped here on purpose: with no P2P
        // engine they can never become playable, and storing them would
        // make bestFor() hand back phantom "sources".
    }


    m_results[key][addonId] = parsed;

    emit streamsUpdated(key.section(QLatin1Char('/'), 0, 0),
                        key.section(QLatin1Char('/'), 1));

    if (arrivedCount(key) >= expectedAddons(key)) {
        const QVariantMap best = bestFor(key.section(QLatin1Char('/'), 0, 0),
                                         key.section(QLatin1Char('/'), 1));
        emit resolutionComplete(key.section(QLatin1Char('/'), 0, 0),
                                key.section(QLatin1Char('/'), 1), best);
    }
}

QVariantMap StreamResolver::bestFor(const QString& type,
                                    const QString& imdbId) const
{
    const QString key = type + QLatin1Char('/') + imdbId;
    const auto perAddon = m_results.value(key);
    if (perAddon.isEmpty()) return {};

    // First direct stream in addon order wins; addons earlier in the user's
    // list are considered better sources.
    for (const auto& addonId : m_addonOrder) {
        const auto it = perAddon.find(addonId);
        if (it == perAddon.end()) continue;
        for (const auto& s : *it)
            if (s.playableDirect())
                return toVariant(s);
    }
    return {};
}

int StreamResolver::expectedAddons(const QString&) const
{ return m_addonOrder.size(); }

bool StreamResolver::isComplete(const QString& type,
                                const QString& imdbId) const
{
    const QString key = type + QLatin1Char('/') + imdbId;
    return arrivedCount(key) >= expectedAddons(key);
}

int StreamResolver::arrivedCount(const QString& key) const
{ return m_results.value(key).size(); }

// ---- network path (thin; shares the ingest contract) -------------------------

void StreamResolver::resolve(const QString& type, const QString& imdbId)
{
    static QNetworkAccessManager nam;   // process-lifetime, fine for skeleton

    const QString key = type + QLatin1Char('/') + imdbId;
    if (m_results.contains(key)) return;    // already answered for this key

    for (const auto& addonId : m_addonOrder) {
        const AddonDescriptor a = m_addons.value(addonId);
        QString base = a.manifestUrl;
        base.remove(QRegularExpression(QStringLiteral("manifest\\.json$")));
        while (base.endsWith(QLatin1Char('/'))) base.chop(1);
        const QUrl u(base + QStringLiteral("/stream/") + type +
                     QLatin1Char('/') + imdbId + QStringLiteral(".json"));
        QNetworkRequest req{u};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setRawHeader("Accept", "application/json");
        auto* rep = nam.get(req);
        connect(rep, &QNetworkReply::finished, this, [this, rep, key, addonId] {
            rep->deleteLater();
            applyAddonStreams(key, addonId,
                              rep->error() == QNetworkReply::NoError
                                  ? rep->readAll()
                                  : QByteArrayLiteral("{\"streams\":[]}"));
        });
    }
}

} // namespace nuvio::playback