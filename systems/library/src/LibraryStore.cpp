#include "nuvio/library/LibraryStore.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>

#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::library {

QString libraryItemKey(const QString& id, const QString& type)
{
    return type.trimmed().toLower() + u':' + id.trimmed();
}

namespace {
constexpr auto kStoreName = "library";
[[nodiscard]] std::string profileKey(const char* base, int profileId)
{
    return std::string(base) + "_" + std::to_string(profileId);
}

LibraryItem itemFromJson(const QJsonObject& o)
{
    LibraryItem it;
    it.id = o.value(QStringLiteral("id")).toString();
    it.type = o.value(QStringLiteral("type")).toString();
    it.name = o.value(QStringLiteral("name")).toString();
    it.poster = o.value(QStringLiteral("poster")).toString();
    it.description = o.value(QStringLiteral("description")).toString();
    it.savedAtEpochMs = static_cast<qint64>(
        o.value(QStringLiteral("savedAtEpochMs")).toDouble(0.0));
    static const char* const kKnown[] = {"id",         "type",
                                         "name",       "poster",
                                         "description", "savedAtEpochMs"};
    it.extra = o;
    for (const char* k : kKnown) it.extra.remove(QLatin1String(k));
    return it;
}

QJsonObject itemToJson(const LibraryItem& it)
{
    QJsonObject o = it.extra;
    o.insert(QStringLiteral("id"), it.id);
    o.insert(QStringLiteral("type"), it.type);
    o.insert(QStringLiteral("name"), it.name);
    o.insert(QStringLiteral("poster"), it.poster);
    o.insert(QStringLiteral("description"), it.description);
    o.insert(QStringLiteral("savedAtEpochMs"),
             static_cast<double>(it.savedAtEpochMs));
    return o;
}

QList<LibrarySyncKey> keysFromJson(const QJsonValue& v)
{
    QList<LibrarySyncKey> out;
    for (const QJsonValue& e : v.toArray()) {
        const QJsonObject o = e.toObject();
        LibrarySyncKey k;
        k.contentId = o.value(QStringLiteral("content_id")).toString();
        k.contentType = o.value(QStringLiteral("content_type")).toString();
        if (!k.contentId.isEmpty()) out.append(k);
    }
    return out;
}

QJsonArray keysToJson(const QList<LibrarySyncKey>& keys)
{
    QJsonArray out;
    for (const LibrarySyncKey& k : keys)
        out.append(QJsonObject{
            {QStringLiteral("content_id"), k.contentId},
            {QStringLiteral("content_type"), k.contentType},
        });
    return out;
}
} // namespace

QList<LibraryItem> LibraryCodec::decodeItems(const QString& json)
{
    const QJsonObject root =
        QJsonDocument::fromJson(json.toUtf8()).object();
    QList<LibraryItem> out;
    for (const QJsonValue& v :
         root.value(QStringLiteral("items")).toArray()) {
        LibraryItem it = itemFromJson(v.toObject());
        if (!it.id.isEmpty()) out.append(it);
    }
    return out;
}

QString LibraryCodec::encodeItems(const QList<LibraryItem>& items)
{
    QList<LibraryItem> sorted = items;
    std::sort(sorted.begin(), sorted.end(),
              [](const LibraryItem& a, const LibraryItem& b) {
                  return a.savedAtEpochMs > b.savedAtEpochMs;
              });
    QJsonArray arr;
    for (const LibraryItem& it : sorted) arr.append(itemToJson(it));
    return QString::fromUtf8(QJsonDocument(QJsonObject{
                                 {QStringLiteral("items"), arr},
                             })
                                 .toJson(QJsonDocument::Compact));
}

LibraryStore::LibraryStore(int profileId, QObject* parent)
    : QObject(parent), m_profileId(profileId)
{
    load();
}

void LibraryStore::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    m_profileId = profileId;
    load();
    emit changed();
}

QVariantList LibraryStore::itemsVariant() const
{
    QVariantList out;
    for (const LibraryItem& it : m_items)
        out.append(QVariantMap{
            {QStringLiteral("id"), it.id},
            {QStringLiteral("type"), it.type},
            {QStringLiteral("name"), it.name},
            {QStringLiteral("poster"), it.poster},
            {QStringLiteral("description"), it.description},
            {QStringLiteral("savedAtEpochMs"),
             static_cast<qint64>(it.savedAtEpochMs)},
        });
    return out;
}

int LibraryStore::count() const { return m_items.size(); }

QList<LibraryItem> LibraryStore::items() const { return m_items; }

bool LibraryStore::isInLibrary(const QString& type, const QString& id) const
{
    const QString key = libraryItemKey(id, type);
    for (const LibraryItem& it : m_items) {
        if (libraryItemKey(it.id, it.type) == key) return true;
    }
    return false;
}

void LibraryStore::addToLibrary(const QString& type, const QString& id,
                                const QString& name, const QString& poster,
                                const QString& description, qint64 nowEpochMs)
{
    if (id.trimmed().isEmpty()) return;
    const QString key = libraryItemKey(id, type);
    bool found = false;
    for (LibraryItem& it : m_items) {
        if (libraryItemKey(it.id, it.type) == key) {
            // Re-add refreshes recency AND display fields (callers always
            // pass current metadata; stale posters/names must not stick).
            it.name = name;
            it.poster = poster;
            it.description = description;
            it.savedAtEpochMs = nowEpochMs;
            found = true;
        }
    }
    if (!found) {
        LibraryItem it;
        it.id = id.trimmed();
        it.type = type.trimmed();
        it.name = name;
        it.poster = poster;
        it.description = description;
        it.savedAtEpochMs = nowEpochMs;
        m_items.append(it);
    }
    // Dirty-mark for the sync leg (re-adding clears a pending delete).
    m_pendingDeletes.erase(
        std::remove_if(m_pendingDeletes.begin(), m_pendingDeletes.end(),
                       [&](const LibrarySyncKey& k) {
                           return libraryItemKey(k.contentId,
                                                 k.contentType) == key;
                       }),
        m_pendingDeletes.end());
    const LibrarySyncKey up{id.trimmed(), type.trimmed()};
    auto same = [&](const LibrarySyncKey& k) {
        return libraryItemKey(k.contentId, k.contentType) == key;
    };
    if (std::none_of(m_pendingUpserts.begin(), m_pendingUpserts.end(), same))
        m_pendingUpserts.append(up);
    persist();
    emit changed();
}

void LibraryStore::removeFromLibrary(const QString& type, const QString& id)
{
    const QString key = libraryItemKey(id, type);
    const int before = m_items.size();
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                 [&](const LibraryItem& it) {
                                     return libraryItemKey(it.id, it.type) ==
                                            key;
                                 }),
                  m_items.end());
    if (m_items.size() == before) return;
    m_pendingUpserts.erase(
        std::remove_if(m_pendingUpserts.begin(), m_pendingUpserts.end(),
                       [&](const LibrarySyncKey& k) {
                           return libraryItemKey(k.contentId,
                                                 k.contentType) == key;
                       }),
        m_pendingUpserts.end());
    const LibrarySyncKey del{id.trimmed(), type.trimmed()};
    auto same = [&](const LibrarySyncKey& k) {
        return libraryItemKey(k.contentId, k.contentType) == key;
    };
    if (std::none_of(m_pendingDeletes.begin(), m_pendingDeletes.end(), same))
        m_pendingDeletes.append(del);
    persist();
    emit changed();
}

QJsonObject LibraryStore::loadPayload() const
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStoreName));
    const auto raw = store.getString(profileKey("library", m_profileId));
    if (!raw || raw->empty()) return {};
    return QJsonDocument::fromJson(QByteArray::fromStdString(*raw)).object();
}

void LibraryStore::savePayload(const QJsonObject& payload)
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStoreName));
    store.putString(profileKey("library", m_profileId),
                    QString::fromUtf8(
                        QJsonDocument(payload).toJson(QJsonDocument::Compact))
                        .toStdString());
}

void LibraryStore::load()
{
    const QJsonObject payload = loadPayload();
    m_items.clear();
    m_pendingUpserts.clear();
    m_pendingDeletes.clear();
    m_deltaCursorEventId = 0;
    m_deltaInitialized = false;
    if (payload.isEmpty()) return;   // fresh profile (or garbage: defaults)
    for (const QJsonValue& v :
         payload.value(QStringLiteral("items")).toArray()) {
        LibraryItem it = itemFromJson(v.toObject());
        if (!it.id.isEmpty()) m_items.append(it);
    }
    m_deltaCursorEventId =
        static_cast<qint64>(payload.value(QStringLiteral("deltaCursorEventId"))
                                .toDouble(0.0));
    m_deltaInitialized =
        payload.value(QStringLiteral("deltaInitialized")).toBool(false);
    m_pendingUpserts = keysFromJson(payload.value(QStringLiteral("pendingUpsertKeys")));
    m_pendingDeletes = keysFromJson(payload.value(QStringLiteral("pendingDeleteKeys")));
}

void LibraryStore::persist()
{    QList<LibraryItem> sorted = m_items;
    std::sort(sorted.begin(), sorted.end(),
              [](const LibraryItem& a, const LibraryItem& b) {
                  return a.savedAtEpochMs > b.savedAtEpochMs;
              });
    QJsonArray arr;
    for (const LibraryItem& it : sorted) arr.append(itemToJson(it));
    savePayload(QJsonObject{
        {QStringLiteral("items"), arr},
        {QStringLiteral("deltaCursorEventId"),
         static_cast<double>(m_deltaCursorEventId)},
        {QStringLiteral("deltaInitialized"), m_deltaInitialized},
        {QStringLiteral("pendingUpsertKeys"),
         keysToJson(m_pendingUpserts)},
        {QStringLiteral("pendingDeleteKeys"),
         keysToJson(m_pendingDeletes)},
    });
}

QList<LibrarySyncKey> LibraryStore::pendingUpserts() const
{
    return m_pendingUpserts;
}

QList<LibrarySyncKey> LibraryStore::pendingDeletes() const
{
    return m_pendingDeletes;
}

qint64 LibraryStore::deltaCursorEventId() const
{
    return m_deltaCursorEventId;
}

bool LibraryStore::deltaInitialized() const { return m_deltaInitialized; }

namespace {
bool keyIn(const QList<LibrarySyncKey>& keys, const QString& itemKey)
{
    for (const LibrarySyncKey& k : keys) {
        if (libraryItemKey(k.contentId, k.contentType) == itemKey)
            return true;
    }
    return false;
}
} // namespace

void LibraryStore::clearPendingUpserts(const QList<LibrarySyncKey>& keys)
{
    const int before = m_pendingUpserts.size();
    m_pendingUpserts.erase(
        std::remove_if(m_pendingUpserts.begin(), m_pendingUpserts.end(),
                       [&](const LibrarySyncKey& k) {
                           return keyIn(keys, libraryItemKey(
                                                 k.contentId, k.contentType));
                       }),
        m_pendingUpserts.end());
    if (m_pendingUpserts.size() != before) persist();
}

void LibraryStore::clearPendingDeletes(const QList<LibrarySyncKey>& keys)
{
    const int before = m_pendingDeletes.size();
    m_pendingDeletes.erase(
        std::remove_if(m_pendingDeletes.begin(), m_pendingDeletes.end(),
                       [&](const LibrarySyncKey& k) {
                           return keyIn(keys, libraryItemKey(
                                                 k.contentId, k.contentType));
                       }),
        m_pendingDeletes.end());
    if (m_pendingDeletes.size() != before) persist();
}

void LibraryStore::setDeltaCursor(qint64 eventId, bool initialized)
{
    if (m_deltaCursorEventId == eventId && m_deltaInitialized == initialized)
        return;
    m_deltaCursorEventId = eventId;
    m_deltaInitialized = initialized;
    persist();
}

void LibraryStore::replaceItems(const QList<LibraryItem>& items)
{
    m_items = items;
    persist();
    emit changed();
}

void LibraryStore::upsertRemoteItems(const QList<LibraryItem>& items)
{
    if (items.isEmpty()) return;
    bool touched = false;
    for (const LibraryItem& remote : items) {
        const QString key = libraryItemKey(remote.id, remote.type);
        bool found = false;
        for (LibraryItem& local : m_items) {
            if (libraryItemKey(local.id, local.type) == key) {
                local = remote;
                found = true;
                touched = true;
            }
        }
        if (!found) {
            m_items.append(remote);
            touched = true;
        }
    }
    if (!touched) return;
    persist();
    emit changed();
}

void LibraryStore::removeRemoteKeys(const QList<LibrarySyncKey>& keys)
{
    if (keys.isEmpty()) return;
    const int before = m_items.size();
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                 [&](const LibraryItem& it) {
                                     return keyIn(keys, libraryItemKey(
                                                           it.id, it.type));
                                 }),
                  m_items.end());
    if (m_items.size() == before) return;
    persist();
    emit changed();
}

} // namespace nuvio::library
