#include "nuvio/watching/ProgressSyncCodec.h"

#include <QJsonArray>

namespace nuvio::watching {

namespace {
QJsonObject entryJson(const WatchEntry& e)
{
    QJsonObject o;
    o.insert("content_id", QString::fromStdString(e.parentMetaId));
    o.insert("content_type", QString::fromStdString(e.contentType));
    o.insert("video_id", QString::fromStdString(e.videoId));
    // kotlinx explicitNulls default: nullable season/episode serialize as
    // null when absent — always present as keys either way.
    if (e.season) o.insert("season", *e.season);
    else          o.insert("season", QJsonValue::Null);
    if (e.episode) o.insert("episode", *e.episode);
    else           o.insert("episode", QJsonValue::Null);
    o.insert("position", static_cast<qint64>(e.lastPositionMs));
    o.insert("duration", static_cast<qint64>(e.durationMs));
    o.insert("last_watched", static_cast<qint64>(e.lastUpdatedEpochMs));
    o.insert("progress_key",
             QString::fromStdString(e.resolvedProgressKey()));
    return o;
}

std::optional<int> optIntOf(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isNull() || !v.isDouble()) return std::nullopt;
    const double d = v.toDouble();
    return static_cast<int>(d);
}

long long int64Of(const QJsonObject& o, const char* key)
{
    return static_cast<long long>(o.value(QLatin1String(key)).toDouble());
}

std::string strOf(const QJsonObject& o, const char* key)
{
    return o.value(QLatin1String(key)).toString().toStdString();
}

WatchEntry entryOf(const QJsonObject& o)
{
    WatchEntry e;
    e.parentMetaId = strOf(o, "content_id");
    e.contentType  = strOf(o, "content_type");
    e.videoId      = strOf(o, "video_id");
    e.season       = optIntOf(o, "season");
    e.episode      = optIntOf(o, "episode");
    e.lastPositionMs     = int64Of(o, "position");
    e.durationMs         = int64Of(o, "duration");
    e.lastUpdatedEpochMs = int64Of(o, "last_watched");
    e.progressKey  = strOf(o, "progress_key");
    e.isCompleted  = false;   // normalized downstream by store load
    return e;
}

QJsonArray entriesArray(const std::vector<WatchEntry>& entries)
{
    QJsonArray arr;
    for (const auto& e : entries) arr.append(entryJson(e));
    return arr;
}
} // namespace

QJsonObject ProgressSyncCodec::syncEntryJson(const WatchEntry& e)
{
    return entryJson(e);
}

QJsonObject ProgressSyncCodec::pushParams(
    int profileId, const std::vector<WatchEntry>& entries,
    const QString& originClientId)
{
    return QJsonObject{
        {QStringLiteral("p_profile_id"), profileId},
        {QStringLiteral("p_entries"), entriesArray(entries)},
        {QStringLiteral("p_origin_client_id"), originClientId},
    };
}

QJsonObject ProgressSyncCodec::deleteParams(
    int profileId, const std::vector<std::string>& progressKeys,
    const QString& originClientId)
{
    QJsonArray keys;
    for (const auto& k : progressKeys) keys.append(
        QString::fromStdString(k));
    return QJsonObject{
        {QStringLiteral("p_profile_id"), profileId},
        {QStringLiteral("p_keys"), keys},
        {QStringLiteral("p_origin_client_id"), originClientId},
    };
}

QJsonObject ProgressSyncCodec::cursorParams(int profileId)
{
    return QJsonObject{{QStringLiteral("p_profile_id"), profileId}};
}

QJsonObject ProgressSyncCodec::deltaPullParams(
    int profileId, long long sinceEventId, int limit)
{
    return QJsonObject{
        {QStringLiteral("p_profile_id"), profileId},
        {QStringLiteral("p_since_event_id"),
         static_cast<qint64>(sinceEventId)},
        {QStringLiteral("p_limit"), limit},
    };
}

QJsonObject ProgressSyncCodec::fullPullParams(int profileId)
{
    // Compose omits p_since_last_watched/p_limit when null.
    return QJsonObject{{QStringLiteral("p_profile_id"), profileId}};
}

std::optional<long long> ProgressSyncCodec::parseCursor(
    const QByteArray& rawBody)
{
    // postgrest scalar function result: bare number token.
    const QByteArray trimmed = rawBody.trimmed();
    if (trimmed.isEmpty()) return std::nullopt;
    bool ok = false;
    const long long v = trimmed.toLongLong(&ok);
    return ok ? std::optional<long long>(v) : std::nullopt;
}

std::vector<ProgressSyncCodec::DeltaEvent> ProgressSyncCodec::decodeDeltas(
    const QJsonDocument& doc)
{
    std::vector<DeltaEvent> out;
    const QJsonArray rows = doc.isArray() ? doc.array() : QJsonArray();
    for (const auto& v : rows) {
        const QJsonObject o = v.toObject();
        DeltaEvent d;
        d.eventId     = int64Of(o, "event_id");
        d.operation   = strOf(o, "operation");
        d.progressKey = strOf(o, "progress_key");
        d.contentId   = strOf(o, "content_id");
        d.contentType = strOf(o, "content_type");
        d.videoId     = strOf(o, "video_id");
        d.season      = optIntOf(o, "season");
        d.episode     = optIntOf(o, "episode");
        d.position    = int64Of(o, "position");
        d.duration    = int64Of(o, "duration");
        d.lastWatched = int64Of(o, "last_watched");
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<WatchEntry> ProgressSyncCodec::decodeRecords(
    const QJsonDocument& doc)
{
    std::vector<WatchEntry> out;
    const QJsonArray rows = doc.isArray() ? doc.array() : QJsonArray();
    for (const auto& v : rows)
        if (v.isObject()) out.push_back(entryOf(v.toObject()));
    return out;
}

// ---- watched-items family ----------------------------------------------------

QJsonObject ProgressSyncCodec::watchedItemJson(const WatchedItem& w)
{
    QJsonObject o;
    o.insert("content_id", QString::fromStdString(w.id));
    o.insert("content_type", QString::fromStdString(w.type));
    // title has a kotlinx default "" -> always emitted (encodeDefaults).
    o.insert("title", QString::fromStdString(w.name));
    if (w.season) o.insert("season", *w.season);
    else          o.insert("season", QJsonValue::Null);
    if (w.episode) o.insert("episode", *w.episode);
    else           o.insert("episode", QJsonValue::Null);
    o.insert("watched_at", static_cast<qint64>(w.markedAtEpochMs));
    return o;
}

QJsonObject ProgressSyncCodec::watchedPushParams(
    int profileId, const std::vector<WatchedItem>& items,
    const QString& originClientId)
{
    QJsonArray arr;
    for (const auto& w : items) arr.append(watchedItemJson(w));
    return QJsonObject{
        {QStringLiteral("p_profile_id"), profileId},
        {QStringLiteral("p_items"), arr},
        {QStringLiteral("p_origin_client_id"), originClientId},
    };
}

QJsonObject ProgressSyncCodec::watchedDeleteParams(
    int profileId, const std::vector<WatchedItem>& items,
    const QString& originClientId)
{
    QJsonArray keys;
    for (const auto& w : items) {
        QJsonObject k;
        k.insert("content_id", QString::fromStdString(w.id));
        if (w.season) k.insert("season", *w.season);
        else          k.insert("season", QJsonValue::Null);
        if (w.episode) k.insert("episode", *w.episode);
        else           k.insert("episode", QJsonValue::Null);
        keys.append(k);
    }
    return QJsonObject{
        {QStringLiteral("p_profile_id"), profileId},
        {QStringLiteral("p_keys"), keys},
        {QStringLiteral("p_origin_client_id"), originClientId},
    };
}

QJsonObject ProgressSyncCodec::watchedPagePullParams(
    int profileId, int page, int pageSize)
{
    return QJsonObject{
        {QStringLiteral("p_profile_id"), profileId},
        {QStringLiteral("p_page"), page},
        {QStringLiteral("p_page_size"), pageSize},
    };
}

std::vector<WatchedItem> ProgressSyncCodec::decodeWatchedRecords(
    const QJsonDocument& doc)
{
    std::vector<WatchedItem> out;
    const QJsonArray rows = doc.isArray() ? doc.array() : QJsonArray();
    for (const auto& v : rows) {
        const QJsonObject o = v.toObject();
        WatchedItem w;
        w.id     = strOf(o, "content_id");
        w.type   = strOf(o, "content_type");
        w.name   = strOf(o, "title");
        w.season = optIntOf(o, "season");
        w.episode = optIntOf(o, "episode");
        w.markedAtEpochMs = int64Of(o, "watched_at");
        if (!w.id.empty()) out.push_back(std::move(w));
    }
    return out;
}

std::vector<ProgressSyncCodec::WatchedDeltaEvent>
ProgressSyncCodec::decodeWatchedDeltas(const QJsonDocument& doc)
{
    std::vector<WatchedDeltaEvent> out;
    const QJsonArray rows = doc.isArray() ? doc.array() : QJsonArray();
    for (const auto& v : rows) {
        const QJsonObject o = v.toObject();
        WatchedDeltaEvent d;
        d.eventId     = int64Of(o, "event_id");
        d.operation   = strOf(o, "operation");
        d.contentId   = strOf(o, "content_id");
        d.contentType = strOf(o, "content_type");
        d.title       = strOf(o, "title");
        d.season      = optIntOf(o, "season");
        d.episode     = optIntOf(o, "episode");
        d.watchedAt   = int64Of(o, "watched_at");
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace nuvio::watching