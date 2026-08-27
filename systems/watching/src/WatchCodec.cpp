#include "nuvio/watching/WatchCodec.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace nuvio::watching {
namespace {

// Read a string field as optional (nullopt when missing or null) — mirrors
// kotlin's String? decode (ignoreUnknownKeys + absent = default null).
std::optional<std::string> optStr(const QJsonObject& o, const QString& k)
{
    if (!o.contains(k) || o.value(k).isNull()) return std::nullopt;
    return o.value(k).toString().toStdString();
}

std::string reqStr(const QJsonObject& o, const QString& k)
{
    return o.value(k).toString().toStdString();
}

std::optional<int> optInt(const QJsonObject& o, const QString& k)
{
    if (!o.contains(k) || o.value(k).isNull()) return std::nullopt;
    return o.value(k).toInt(-1);
}

std::optional<float> optFloat(const QJsonObject& o, const QString& k)
{
    if (!o.contains(k) || o.value(k).isNull()) return std::nullopt;
    return o.value(k).toDouble();
}

void putOpt(QJsonObject& o, const QString& k, const std::optional<std::string>& v)
{
    if (v) o.insert(k, QString::fromStdString(*v));
}
void putOpt(QJsonObject& o, const QString& k, const std::optional<int>& v)
{
    if (v) o.insert(k, *v);
}
void putOpt(QJsonObject& o, const QString& k, const std::optional<float>& v)
{
    if (v) o.insert(k, *v);
}

WatchEntry decodeEntry(const QJsonObject& o)
{
    WatchEntry e;
    e.contentType          = reqStr(o, "contentType");
    e.parentMetaId         = reqStr(o, "parentMetaId");
    e.parentMetaType       = reqStr(o, "parentMetaType");
    e.videoId              = reqStr(o, "videoId");
    e.title                = reqStr(o, "title");
    e.logo                 = optStr(o, "logo");
    e.poster               = optStr(o, "poster");
    e.background           = optStr(o, "background");
    e.season               = optInt(o, "seasonNumber");
    e.episode              = optInt(o, "episodeNumber");
    e.episodeTitle         = optStr(o, "episodeTitle");
    e.episodeThumbnail     = optStr(o, "episodeThumbnail");
    e.lastPositionMs       = o.value("lastPositionMs").toInteger();
    e.durationMs           = o.value("durationMs").toInteger();
    e.lastUpdatedEpochMs   = o.value("lastUpdatedEpochMs").toInteger();
    e.providerName         = optStr(o, "providerName");
    e.providerAddonId      = optStr(o, "providerAddonId");
    e.lastStreamTitle      = optStr(o, "lastStreamTitle");
    e.lastStreamSubtitle   = optStr(o, "lastStreamSubtitle");
    e.pauseDescription     = optStr(o, "pauseDescription");
    e.lastSourceUrl        = optStr(o, "lastSourceUrl");
    e.isCompleted          = o.value("isCompleted").toBool(false);
    e.progressPercent      = optFloat(o, "progressPercent");
    e.source               = reqStr(o, "source");
    e.trackingProviderId   = optStr(o, "trackingProviderId");
    e.trackingProviderItemId= optStr(o, "trackingProviderItemId");
    e.trackingSourceUrl    = optStr(o, "trackingSourceUrl");
    e.progressKey          = optStr(o, "progressKey");
    return e;
}

} // namespace

StoredProgressPayload WatchCodec::decodeProgress(const QString& json)
{
    StoredProgressPayload payload;
    if (json.isEmpty()) return payload;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return payload;
    const QJsonObject root = doc.object();
    payload.lastSuccessfulPushEpochMs = root.value("lastSuccessfulPushEpochMs").toInteger();
    payload.deltaCursorEventId   = root.value("deltaCursorEventId").toInteger();
    payload.deltaInitialized     = root.value("deltaInitialized").toBool(false);

    const QJsonArray arr = root.value("entries").toArray();
    payload.entries.reserve(arr.size());
    for (const QJsonValue& v : arr)
        if (v.isObject())
            payload.entries.push_back(decodeEntry(v.toObject()));

        const QJsonArray dirty = root.value("dirtyProgressKeys").toArray();
    for (const QJsonValue& v : dirty)
        if (v.isString())
            payload.dirtyProgressKeys.push_back(v.toString().toStdString());
    return payload;
}

// --- encode side (matches WatchProgressCodec.encodeEntries) ------------------

static QJsonObject entryToJson(const WatchEntry& e)
{
    // Compose's normalizedCompletion().withResolvedProgressKey(): persist the
    // resolved progressKey AND a normalized completed/percent.
    WatchEntry ne = e;
    ne.progressKey = e.resolvedProgressKey();
    ne.isCompleted = ne.isEffectivelyCompleted();
    if (ne.isCompleted) {
        ne.progressPercent = 100.0f;
    } else if (e.progressPercent) {
        ne.progressPercent = *e.progressPercent;
    }

    QJsonObject o;
    o.insert("contentType", QString::fromStdString(ne.contentType));
    o.insert("parentMetaId", QString::fromStdString(ne.parentMetaId));
    o.insert("parentMetaType", QString::fromStdString(ne.parentMetaType));
    o.insert("videoId", QString::fromStdString(ne.videoId));
    o.insert("title", QString::fromStdString(ne.title));
    putOpt(o, "logo", ne.logo);
    putOpt(o, "poster", ne.poster);
    putOpt(o, "background", ne.background);
    putOpt(o, "seasonNumber", ne.season);
    putOpt(o, "episodeNumber", ne.episode);
    putOpt(o, "episodeTitle", ne.episodeTitle);
    putOpt(o, "episodeThumbnail", ne.episodeThumbnail);
    o.insert("lastPositionMs", static_cast<qint64>(ne.lastPositionMs));
    o.insert("durationMs", static_cast<qint64>(ne.durationMs));
    o.insert("lastUpdatedEpochMs", static_cast<qint64>(ne.lastUpdatedEpochMs));
    putOpt(o, "providerName", ne.providerName);
    putOpt(o, "providerAddonId", ne.providerAddonId);
    putOpt(o, "lastStreamTitle", ne.lastStreamTitle);
    putOpt(o, "lastStreamSubtitle", ne.lastStreamSubtitle);
    putOpt(o, "pauseDescription", ne.pauseDescription);
    putOpt(o, "lastSourceUrl", ne.lastSourceUrl);
    o.insert("isCompleted", ne.isCompleted);
    putOpt(o, "progressPercent", ne.progressPercent);
    o.insert("source", QString::fromStdString(ne.source));
    putOpt(o, "trackingProviderId", ne.trackingProviderId);
    putOpt(o, "trackingProviderItemId", ne.trackingProviderItemId);
    putOpt(o, "trackingSourceUrl", ne.trackingSourceUrl);
    o.insert("progressKey", QString::fromStdString(ne.progressKey.value_or(ne.resolvedProgressKey())));
    return o;
}

QString WatchCodec::encodeProgressPayload(const StoredProgressPayload& p)
{
    QJsonObject root;
    QJsonArray arr;
    // (no reserve: QJsonArray lacks it pre-Qt6.3)
    for (const WatchEntry& e : p.entries)
            arr.append(entryToJson(e));
    root.insert("entries", arr);
    // Sync bookkeeping envelope: the CALLER owns these values — preserving
    // them across Qt writes is the whole point of this entry point.
    root.insert("lastSuccessfulPushEpochMs",
                static_cast<qint64>(p.lastSuccessfulPushEpochMs));
    root.insert("deltaCursorEventId",
                static_cast<qint64>(p.deltaCursorEventId));
    root.insert("deltaInitialized", p.deltaInitialized);
    QJsonArray dirty;
    for (const auto& k : p.dirtyProgressKeys) dirty.append(
        QString::fromStdString(k));
    root.insert("dirtyProgressKeys", dirty);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString WatchCodec::encodeProgress(const std::vector<WatchEntry>& entries)
{
    StoredProgressPayload p;
    p.entries = entries;   // zero envelope — legacy entries-only path
    return encodeProgressPayload(p);
}

QString WatchCodec::encodeWatchedPayload(const StoredWatchedPayload& p)
{
    QJsonObject root;
    QJsonArray arr;
    for (const WatchedItem& w : p.items) {
        QJsonObject o;
        o.insert("type", QString::fromStdString(w.type));
        o.insert("id", QString::fromStdString(w.id));
        o.insert("name", QString::fromStdString(w.name));
        putOpt(o, "poster", w.poster);
        putOpt(o, "releaseInfo", w.releaseInfo);
        putOpt(o, "season", w.season);
        putOpt(o, "episode", w.episode);
        putOpt(o, "videoId", w.videoId);
        o.insert("markedAtEpochMs", static_cast<qint64>(w.markedAtEpochMs));
        arr.append(o);
    }
    root.insert("items", arr);
    QJsonArray fully, expanded, dirty;
    for (const auto& k : p.fullyWatchedSeriesKeys)
        fully.append(QString::fromStdString(k));
    for (const auto& k : p.expandedSiblingKeys)
        expanded.append(QString::fromStdString(k));
    for (const auto& k : p.dirtyWatchedKeys)
        dirty.append(QString::fromStdString(k));
    root.insert("fullyWatchedSeriesKeys", fully);
    root.insert("expandedSiblingKeys", expanded);
    root.insert("lastSuccessfulPushEpochMs",
                static_cast<qint64>(p.lastSuccessfulPushEpochMs));
    root.insert("deltaCursorEventId",
                static_cast<qint64>(p.deltaCursorEventId));
    root.insert("deltaInitialized", p.deltaInitialized);
    root.insert("dirtyWatchedKeys", dirty);
    root.insert("providerPayloads", QJsonObject());
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString WatchCodec::encodeWatched(const std::vector<WatchedItem>& items)
{
    StoredWatchedPayload p;
    p.items = items;   // zero envelope — legacy items-only path
    return encodeWatchedPayload(p);
}

StoredWatchedPayload WatchCodec::decodeWatchedPayload(const QString& json)
{
    StoredWatchedPayload p;
    if (json.isEmpty()) return p;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return p;
    const QJsonObject root = doc.object();
    p.lastSuccessfulPushEpochMs =
        root.value("lastSuccessfulPushEpochMs").toInteger();
    p.deltaCursorEventId = root.value("deltaCursorEventId").toInteger();
    p.deltaInitialized   = root.value("deltaInitialized").toBool(false);
    const QJsonArray arr = root.value("items").toArray();
    for (const QJsonValue& v : arr)
        if (v.isObject()) p.items.push_back(decodeWatchedItem(v.toObject()));
    const QJsonArray fully = root.value("fullyWatchedSeriesKeys").toArray();
    for (const QJsonValue& v : fully)
        if (v.isString())
            p.fullyWatchedSeriesKeys.push_back(v.toString().toStdString());
    const QJsonArray expanded = root.value("expandedSiblingKeys").toArray();
    for (const QJsonValue& v : expanded)
        if (v.isString())
            p.expandedSiblingKeys.push_back(v.toString().toStdString());
    const QJsonArray dirty = root.value("dirtyWatchedKeys").toArray();
    for (const QJsonValue& v : dirty)
        if (v.isString())
            p.dirtyWatchedKeys.push_back(v.toString().toStdString());
    return p;
}

std::vector<WatchedItem> WatchCodec::decodeWatched(const QString& json)
{
    return decodeWatchedPayload(json).items;
}

WatchedItem WatchCodec::decodeWatchedItem(const QJsonObject& o)
{
    WatchedItem w;
    w.type   = o.value("type").toString().toStdString();
    w.id     = o.value("id").toString().toStdString();
    w.name   = o.value("name").toString().toStdString();
    w.poster = optStr(o, "poster");
    w.releaseInfo = optStr(o, "releaseInfo");
    w.season = optInt(o, "season");
    w.episode = optInt(o, "episode");
    w.videoId = optStr(o, "videoId");
    w.markedAtEpochMs = o.value("markedAtEpochMs").toInteger();
    return w;
}


} // namespace nuvio::watching
