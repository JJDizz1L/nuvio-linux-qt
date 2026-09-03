#include "nuvio/playback/StreamLinkCache.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::playback {

namespace {
// Verbatim PlaybackUrlCredentials table (lowercased, compacted compare).
bool keyLooksCredential(const QString& rawKey)
{
    const QString k = rawKey.trimmed().toLower();
    if (k.isEmpty()) return false;
    static const char* const kKeys[] = {
        "accesskey",      "accesssignature", "accesssig",  "access_token",
        "accesstoken",    "auth",            "authkey",    "authsig",
        "authsignature",  "auth_token",      "authtoken",  "e",
        "exp",            "expiration",      "expire",     "expires",
        "expiresat",      "expiresin",       "expires_in", "expiry",
        "hmac",           "jwt",             "keypairid",  "policy",
        "sig",            "signature",       "signed",     "st",
        "t",              "token",
    };
    QString compact;
    compact.reserve(k.size());
    for (const QChar c : k) {
        if (c != u'-' && c != u'_' && c != u'.') compact.append(c);
    }
    for (const char* known : kKeys) {
        if (k == QLatin1String(known) || compact == QLatin1String(known))
            return true;
    }
    static const char* const kFrags[] = {"token", "signature", "expires",
                                         "expiry"};
    for (const char* f : kFrags) {
        if (k.contains(QLatin1String(f)) || compact.contains(QLatin1String(f)))
            return true;
    }
    return false;
}
} // namespace

QString streamLinkContentKey(const QString& type, const QString& videoId,
                             const QString& parentMetaId, int season,
                             int episode)
{
    const QString t = type.toLower();
    if (!parentMetaId.trimmed().isEmpty() && season >= 0 && episode >= 0)
        return t + u'|' + parentMetaId.trimmed() + u"|s" +
               QString::number(season) + u"|e" + QString::number(episode) +
               u'|' + videoId;
    return t + u'|' + videoId;
}

QString streamLinkHashedKey(const QString& contentKey)
{
    unsigned long long acc = 0;
    for (const QChar c : contentKey)
        acc = acc * 31ULL + static_cast<unsigned long long>(c.unicode());
    return QStringLiteral("stream_link_") + QString::number(acc);
}

bool urlHasExpiringCredentials(const QString& url)
{
    const int q = url.indexOf(u'?');
    if (q < 0) return false;
    QString query = url.mid(q + 1);
    const int h = query.indexOf(u'#');
    if (h >= 0) query.truncate(h);
    if (query.trimmed().isEmpty()) return false;
    const QStringList params = query.split(u'&');
    for (const QString& p : params) {
        for (const QString& piece : p.split(u';')) {
            const QString key = piece.section(u'=', 0, 0);
            if (keyLooksCredential(key)) return true;
        }
    }
    return false;
}

StreamLinkCache::StreamLinkCache(int profileId) : m_profileId(profileId) {}

void StreamLinkCache::save(const QString& contentKey, const CachedLink& link,
                           qint64 nowEpochMs)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("stream_link_cache"));
    const QString key =
        streamLinkHashedKey(contentKey) + u'_' + QString::number(m_profileId);
    if (link.url.trimmed().isEmpty() ||
        urlHasExpiringCredentials(link.url)) {
        store.remove(key.toStdString());
        return;
    }
    const QJsonObject o{
        {QStringLiteral("url"), link.url},
        {QStringLiteral("streamName"), link.streamName},
        {QStringLiteral("addonName"), link.addonName},
        {QStringLiteral("addonId"), link.addonId},
        {QStringLiteral("cachedAtMs"), nowEpochMs},
    };
    store.putString(key.toStdString(),
                    QString::fromUtf8(
                        QJsonDocument(o).toJson(QJsonDocument::Compact))
                        .toStdString());
}

std::optional<CachedLink> StreamLinkCache::getValid(const QString& contentKey,
                                                    qint64 maxAgeMs,
                                                    qint64 nowEpochMs)
{
    if (maxAgeMs <= 0) return std::nullopt;
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("stream_link_cache"));
    const QString key =
        streamLinkHashedKey(contentKey) + u'_' + QString::number(m_profileId);
    const auto raw = store.getString(key.toStdString());
    if (!raw) return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(
                              QByteArray::fromStdString(*raw))
                              .object();
    if (o.isEmpty()) {
        store.remove(key.toStdString());
        return std::nullopt;
    }
    CachedLink out;
    out.url = o.value(QStringLiteral("url")).toString();
    out.streamName = o.value(QStringLiteral("streamName")).toString();
    out.addonName = o.value(QStringLiteral("addonName")).toString();
    out.addonId = o.value(QStringLiteral("addonId")).toString();
    out.cachedAtMs = static_cast<qint64>(
        o.value(QStringLiteral("cachedAtMs")).toDouble(-1.0));
    const qint64 age = nowEpochMs - out.cachedAtMs;
    if (out.cachedAtMs <= 0 || age > maxAgeMs) {
        store.remove(key.toStdString());
        return std::nullopt;
    }
    if (!out.url.trimmed().isEmpty() &&
        urlHasExpiringCredentials(out.url)) {
        store.remove(key.toStdString());
        return std::nullopt;
    }
    if (out.url.trimmed().isEmpty()) {
        store.remove(key.toStdString());
        return std::nullopt;
    }
    return out;
}

} // namespace nuvio::playback
