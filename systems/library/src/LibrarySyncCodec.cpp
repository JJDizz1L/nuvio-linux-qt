#include "nuvio/library/LibrarySyncCodec.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "nuvio/library/LibraryStore.h"

namespace nuvio::library {

QJsonObject LibrarySyncCodec::toSyncItem(const LibraryItem& item)
{
    auto nullStr = [](const QString& s) -> QJsonValue {
        return s.isEmpty() ? QJsonValue() : QJsonValue(s);
    };
    const QJsonObject extra = item.extra;
    QJsonArray genres;
    for (const QJsonValue& g : extra.value(QStringLiteral("genres")).toArray())
        genres.append(g);
    return QJsonObject{
        {QStringLiteral("content_id"), item.id},
        {QStringLiteral("content_type"), item.type},
        {QStringLiteral("name"), item.name},
        {QStringLiteral("poster"), nullStr(item.poster)},
        {QStringLiteral("poster_shape"),
         extra.value(QStringLiteral("posterShape")).toString("POSTER")},
        {QStringLiteral("background"),
         extra.contains(QStringLiteral("background"))
             ? extra.value(QStringLiteral("background"))
             : QJsonValue()},
        {QStringLiteral("description"), nullStr(item.description)},
        {QStringLiteral("release_info"),
         extra.contains(QStringLiteral("releaseInfo"))
             ? extra.value(QStringLiteral("releaseInfo"))
             : QJsonValue()},
        {QStringLiteral("imdb_rating"),
         extra.contains(QStringLiteral("imdbRating"))
             ? extra.value(QStringLiteral("imdbRating"))
             : QJsonValue()},
        {QStringLiteral("genres"), genres},
        {QStringLiteral("addon_base_url"),
         extra.contains(QStringLiteral("addonBaseUrl"))
             ? extra.value(QStringLiteral("addonBaseUrl"))
             : QJsonValue()},
        {QStringLiteral("added_at"),
         static_cast<double>(item.savedAtEpochMs)},
    };
}

LibraryItem LibrarySyncCodec::fromSyncItem(const QJsonObject& o)
{
    LibraryItem it;
    it.id = o.value(QStringLiteral("content_id")).toString();
    it.type = o.value(QStringLiteral("content_type")).toString();
    it.name = o.value(QStringLiteral("name")).toString();
    it.poster = o.value(QStringLiteral("poster")).toString();
    it.description = o.value(QStringLiteral("description")).toString();
    it.savedAtEpochMs = static_cast<qint64>(
        o.value(QStringLiteral("added_at")).toDouble(0.0));
    static const char* const kKnown[] = {
        "content_id", "content_type", "name",  "poster",
        "description", "added_at",
    };
    it.extra = o;
    for (const char* k : kKnown) it.extra.remove(QLatin1String(k));
    return it;
}

QJsonObject LibrarySyncCodec::encodeKey(const LibrarySyncKey& key)
{
    return QJsonObject{
        {QStringLiteral("content_id"), key.contentId},
        {QStringLiteral("content_type"), key.contentType},
    };
}

QList<LibraryDeltaEvent> LibrarySyncCodec::parseDeltaEvents(
    const QByteArray& body)
{
    QList<LibraryDeltaEvent> out;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray{};
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        if (!o.contains(QLatin1String("event_id"))) continue;
        LibraryDeltaEvent e;
        e.eventId = static_cast<qint64>(
            o.value(QStringLiteral("event_id")).toDouble(0.0));
        e.operation = o.value(QStringLiteral("operation")).toString();
        e.item = o;
        out.append(e);
    }
    return out;
}

qint64 LibrarySyncCodec::parseCursor(const QByteArray& raw, qint64 fallback)
{
    bool ok = false;
    const qint64 v = raw.trimmed().toLongLong(&ok);
    return ok ? v : fallback;
}

QJsonObject LibrarySyncCodec::pushItemsParams(
    int profileId, const QList<QJsonObject>& items,
    const QString& originClientId)
{
    QJsonArray arr;
    for (const QJsonObject& o : items) arr.append(o);
    return QJsonObject{
        {QStringLiteral("p_profile_id"), profileId},
        {QStringLiteral("p_items"), arr},
        {QStringLiteral("p_origin_client_id"), originClientId},
    };
}

QJsonObject LibrarySyncCodec::deleteItemsParams(
    int profileId, const QList<LibrarySyncKey>& keys,
    const QString& originClientId)
{
    QJsonArray arr;
    for (const LibrarySyncKey& k : keys) arr.append(encodeKey(k));
    return QJsonObject{
        {QStringLiteral("p_profile_id"), profileId},
        {QStringLiteral("p_keys"), arr},
        {QStringLiteral("p_origin_client_id"), originClientId},
    };
}

} // namespace nuvio::library
