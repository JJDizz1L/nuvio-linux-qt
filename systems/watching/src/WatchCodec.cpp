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

QString WatchCodec::encodeProgress(const std::vector<WatchEntry>& entries)
{
    QJsonObject root;
    QJsonArray arr;
    // (no reserve: QJsonArray lacks it pre-Qt6.3)
    for (const WatchEntry& e : entries)
            arr.append(entryToJson(e));
    root.insert("entries", arr);
    root.insert("lastSuccessfulPushEpochMs", QJsonValue(0));
    root.insert("deltaCursorEventId", QJsonValue(0));
    root.insert("deltaInitialized", false);
    root.insert("dirtyProgressKeys", QJsonArray());
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString WatchCodec::encodeWatched(const std::vector<WatchedItem>& items)
{
    QJsonObject root;
    QJsonArray arr;
    // (no reserve: QJsonArray lacks it pre-Qt6.3)
    for (const WatchedItem& w : items) {
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
    root.insert("fullyWatchedSeriesKeys", QJsonArray());
    root.insert("expandedSiblingKeys", QJsonArray());
    root.insert("lastSuccessfulPushEpochMs", QJsonValue(0));
    root.insert("deltaCursorEventId", QJsonValue(0));
    root.insert("deltaInitialized", false);
    root.insert("dirtyWatchedKeys", QJsonArray());
    root.insert("providerPayloads", QJsonObject());
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::vector<WatchedItem> WatchCodec::decodeWatched(const QString& json)
{
    std::vector<WatchedItem> out;
    if (json.isEmpty()) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return out;
    const QJsonArray arr = doc.object().value("items").toArray();
    // (no reserve)
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
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
        out.push_back(std::move(w));
    }
    return out;
}


} // namespace nuvio::watching
