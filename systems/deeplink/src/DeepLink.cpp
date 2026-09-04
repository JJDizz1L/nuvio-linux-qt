#include "nuvio/deeplink/DeepLink.h"

#include <QUrl>
#include <QUrlQuery>

namespace nuvio::deeplink {

namespace {

QString normalizeMediaType(const QString& value)
{
    const QString v = value.trimmed().toLower();
    if (v == "movie" || v == "movies" || v == "film" || v == "films")
        return QStringLiteral("movie");
    if (v == "series" || v == "show" || v == "shows" || v == "tv" ||
        v == "tvshow" || v == "tvshows")
        return QStringLiteral("series");
    return {};
}

QString normalizeId(const QString& value)
{
    QString id = value.trimmed();
    const QString prefix = QStringLiteral("imdb:");
    if (id.startsWith(prefix, Qt::CaseInsensitive)) id.remove(0, prefix.size());
    id = id.trimmed();
    return id;
}

bool looksLikeAddonHost(const QString& host)
{
    if (host.contains(u'.') || host.compare(QStringLiteral("localhost"),
                                            Qt::CaseInsensitive) == 0)
        return true;
    for (QChar c : host) {
        if (c.isDigit()) return true;
    }
    return false;
}

/// "scheme://rest" -> "https://rest" (blank rest or a "/..." rest is null).
QString customSchemeToHttps(const QString& url, const QString& scheme)
{
    const QString prefix = scheme + QStringLiteral("://");
    QString trimmed = url.trimmed();
    if (!trimmed.startsWith(prefix, Qt::CaseInsensitive)) return {};
    QString rest = trimmed.mid(prefix.size());
    if (rest.trimmed().isEmpty() || rest.startsWith(u'/')) return {};
    return QStringLiteral("https://") + rest;
}

QString firstParameter(const QUrlQuery& query, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString value =
            query.queryItemValue(QLatin1String(key), QUrl::FullyDecoded)
                .trimmed();
        if (!value.isEmpty()) return value;
    }
    return {};
}

DeepLink metaFromParameters(const QUrlQuery& query)
{
    const QString type = normalizeMediaType(
        firstParameter(query, {"type", "mediaType", "media_type"}));
    QString id = normalizeId(
        firstParameter(query, {"id", "imdb", "imdbId", "imdb_id"}));
    if (id.isEmpty()) {
        const QString tmdb =
            firstParameter(query, {"tmdb", "tmdbId", "tmdb_id"});
        if (!tmdb.isEmpty()) {
            QString bare = tmdb;
            const QString prefix = QStringLiteral("tmdb:");
            if (bare.startsWith(prefix, Qt::CaseInsensitive))
                bare.remove(0, prefix.size());
            bare = bare.trimmed();
            if (!bare.isEmpty()) id = prefix + bare;
        }
    }
    if (type.isEmpty() || id.isEmpty()) return {};
    DeepLink out;
    out.kind = DeepLinkKind::Meta;
    out.meta = {type, id};
    return out;
}

DeepLink metaFromPath(const QStringList& segments)
{
    if (segments.size() < 2) return {};
    const QString type = normalizeMediaType(segments[0]);
    const QString id = normalizeId(segments[1]);
    if (type.isEmpty() || id.isEmpty()) return {};
    DeepLink out;
    out.kind = DeepLinkKind::Meta;
    out.meta = {type, id};
    return out;
}

DeepLink providerMeta(const QString& provider, const QStringList& segments,
                      const QUrlQuery& query)
{
    const QString first = segments.value(0);
    const QString second = segments.value(1);
    const QString firstAsType = normalizeMediaType(first);
    const QString queryType = normalizeMediaType(
        firstParameter(query, {"type", "mediaType", "media_type"}));
    const QString type =
        !firstAsType.isEmpty() ? firstAsType : queryType;
    const QString rawId = !firstAsType.isEmpty() ? second : first;
    QString id;
    if (provider == "tmdb") {
        QString bare = rawId.trimmed();
        const QString prefix = QStringLiteral("tmdb:");
        if (bare.startsWith(prefix, Qt::CaseInsensitive))
            bare.remove(0, prefix.size());
        bare = bare.trimmed();
        if (!bare.isEmpty()) id = prefix + bare;
    } else {
        id = normalizeId(rawId);
    }
    if (type.isEmpty() || id.isEmpty()) return {};
    DeepLink out;
    out.kind = DeepLinkKind::Meta;
    out.meta = {type, id};
    return out;
}

QStringList pathSegments(const QUrl& url)
{
    QStringList out;
    for (const QString& part : url.path().split(u'/')) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) out.append(trimmed);
    }
    return out;
}

} // namespace

DeepLink parseDeepLink(const QString& url)
{
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid()) return {};
    const QString scheme = parsed.scheme().toLower();
    if (scheme == "stremio") {
        if (!looksLikeAddonHost(parsed.host().toLower())) return {};
        const QString https = customSchemeToHttps(url, scheme);
        if (https.isEmpty()) return {};
        DeepLink out;
        out.kind = DeepLinkKind::AddonInstall;
        out.addon.manifestUrl = https;
        return out;
    }
    if (scheme != "nuvio") return {};

    const QString host = parsed.host().toLower();
    const QStringList segments = pathSegments(parsed);
    const QUrlQuery query(parsed);
    if (host == "meta") {
        DeepLink byParams = metaFromParameters(query);
        if (byParams.kind != DeepLinkKind::None) return byParams;
        return metaFromPath(segments);
    }
    if (host == "detail" || host == "details" || host == "open" ||
        host == "watch")
        return metaFromPath(segments);
    if (host == "movie" || host == "movies" || host == "series" ||
        host == "show" || host == "shows" || host == "tv") {
        const QString type = normalizeMediaType(host);
        const QString id = normalizeId(segments.value(0));
        if (type.isEmpty() || id.isEmpty()) return {};
        DeepLink out;
        out.kind = DeepLinkKind::Meta;
        out.meta = {type, id};
        return out;
    }
    if (host == "imdb" || host == "tmdb")
        return providerMeta(host, segments, query);
    if (host == "downloads") {
        DeepLink out;
        out.kind = DeepLinkKind::Downloads;
        return out;
    }
    if (host == "auth") return {};   // reserved for tracking callbacks
    if (looksLikeAddonHost(host)) {
        const QString https = customSchemeToHttps(url, scheme);
        if (https.isEmpty()) return {};
        DeepLink out;
        out.kind = DeepLinkKind::AddonInstall;
        out.addon.manifestUrl = https;
        return out;
    }
    return {};
}

QString buildMetaUrl(const QString& type, const QString& id)
{
    return QStringLiteral("nuvio://meta?type=") +
           QString::fromUtf8(
               QUrl::toPercentEncoding(type.trimmed())) +
           QStringLiteral("&id=") +
           QString::fromUtf8(QUrl::toPercentEncoding(id.trimmed()));
}

QString buildDownloadsUrl() { return QStringLiteral("nuvio://downloads"); }

bool isAppUrl(const QString& value)
{
    const QString trimmed = value.trimmed();
    return trimmed.startsWith(QStringLiteral("nuvio://"),
                              Qt::CaseInsensitive) ||
           trimmed.startsWith(QStringLiteral("stremio://"),
                              Qt::CaseInsensitive);
}

} // namespace nuvio::deeplink
