#include "nuvio/library/CollectionStore.h"

#include <algorithm>

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QUuid>

#include "nuvio/library/AddonRegistry.h"
#include "nuvio/library/CatalogService.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::library {

namespace {
[[nodiscard]] std::string profileKey(int profileId)
{
    return "collections_" + std::to_string(profileId);
}

bool isAddonSource(const QJsonObject& o)
{
    const QString provider =
        o.value(QStringLiteral("provider")).toString(QStringLiteral("addon"));
    return provider.compare(QLatin1String("addon"), Qt::CaseInsensitive) == 0;
}

CollectionSource sourceFromJson(const QJsonObject& o)
{
    CollectionSource s;
    s.provider = o.value(QStringLiteral("provider")).toString("addon");
    s.addonId = o.value(QStringLiteral("addonId")).toString();
    s.type = o.value(QStringLiteral("type")).toString();
    s.catalogId = o.value(QStringLiteral("catalogId")).toString();
    s.genre = o.value(QStringLiteral("genre")).toString();
    return s;
}

QJsonObject sourceToJson(const CollectionSource& s)
{
    QJsonObject o{
        {QStringLiteral("provider"), s.provider.isEmpty() ? QStringLiteral("addon") : s.provider},
        {QStringLiteral("addonId"), s.addonId},
        {QStringLiteral("type"), s.type},
        {QStringLiteral("catalogId"), s.catalogId},
    };
    if (!s.genre.isEmpty())
        o.insert(QStringLiteral("genre"), s.genre);
    return o;
}

CollectionFolder folderFromJson(const QJsonObject& o)
{
    CollectionFolder f;
    f.raw = o;
    f.id = o.value(QStringLiteral("id")).toString();
    f.title = o.value(QStringLiteral("title")).toString();
    for (const QJsonValue& v :
         o.value(QStringLiteral("sources")).toArray()) {
        const QJsonObject so = v.toObject();
        if (isAddonSource(so) && !so.value(QStringLiteral("addonId"))
                                      .toString()
                                      .isEmpty())
            f.addonSources.append(sourceFromJson(so));
        else
            f.otherSources.append(so);
    }
    return f;
}

QJsonObject folderToJson(const CollectionFolder& f)
{
    QJsonObject o = f.raw;
    o.insert(QStringLiteral("id"), f.id);
    o.insert(QStringLiteral("title"), f.title);
    QJsonArray sources = f.otherSources;
    for (const CollectionSource& s : f.addonSources)
        sources.append(sourceToJson(s));
    o.insert(QStringLiteral("sources"), sources);
    return o;
}

Collection collectionFromJson(const QJsonObject& o)
{
    Collection c;
    c.raw = o;
    c.id = o.value(QStringLiteral("id")).toString();
    c.title = o.value(QStringLiteral("title")).toString();
    c.pinToTop = o.value(QStringLiteral("pinToTop")).toBool(false);
    c.viewMode =
        o.value(QStringLiteral("viewMode")).toString(QStringLiteral("TABBED_GRID"));
    c.showAllTab = o.value(QStringLiteral("showAllTab")).toBool(true);
    for (const QJsonValue& v :
         o.value(QStringLiteral("folders")).toArray()) {
        CollectionFolder f = folderFromJson(v.toObject());
        if (!f.id.isEmpty()) c.folders.append(f);
    }
    return c;
}

QJsonObject collectionToJson(const Collection& c)
{
    QJsonObject o = c.raw;
    o.insert(QStringLiteral("id"), c.id);
    o.insert(QStringLiteral("title"), c.title);
    o.insert(QStringLiteral("pinToTop"), c.pinToTop);
    o.insert(QStringLiteral("viewMode"), c.viewMode);
    o.insert(QStringLiteral("showAllTab"), c.showAllTab);
    QJsonArray folders;
    for (const CollectionFolder& f : c.folders)
        folders.append(folderToJson(f));
    o.insert(QStringLiteral("folders"), folders);
    return o;
}
} // namespace

QList<Collection> CollectionCodec::decode(const QString& json)
{
    QList<Collection> out;
    const QJsonArray arr =
        QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue& v : arr) {
        Collection c = collectionFromJson(v.toObject());
        if (!c.id.isEmpty()) out.append(c);
    }
    return out;
}

QString CollectionCodec::encode(const QList<Collection>& items)
{
    QJsonArray arr;
    for (const Collection& c : items) arr.append(collectionToJson(c));
    return QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

CollectionStore::CollectionStore(int profileId, QObject* parent)
    : QObject(parent), m_profileId(profileId),
      m_nam(new QNetworkAccessManager(this))
{
    load();
}

void CollectionStore::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    m_profileId = profileId;
    m_folderCollection.clear();
    m_folderId.clear();
    m_folderItems.clear();
    load();
    emit changed();
    emit folderChanged();
}

void CollectionStore::setAddonRegistry(AddonRegistry* registry)
{
    m_registry = registry;
}

QVariantList CollectionStore::collectionsVariant() const
{
    QVariantList out;
    for (const Collection& c : m_collections) out.append(collectionVariant(c));
    return out;
}

QVariantMap CollectionStore::collectionVariant(const Collection& c) const
{
    QVariantList folders;
    for (const CollectionFolder& f : c.folders) {
        QVariantList sources;
        for (const CollectionSource& s : f.addonSources)
            sources.append(QVariantMap{
                {QStringLiteral("provider"), s.provider},
                {QStringLiteral("addonId"), s.addonId},
                {QStringLiteral("type"), s.type},
                {QStringLiteral("catalogId"), s.catalogId},
                {QStringLiteral("genre"), s.genre},
            });
        folders.append(QVariantMap{
            {QStringLiteral("id"), f.id},
            {QStringLiteral("title"), f.title},
            {QStringLiteral("sources"), sources},
            {QStringLiteral("sourceCount"),
             f.addonSources.size() + f.otherSources.size()},
        });
    }
    return QVariantMap{
        {QStringLiteral("id"), c.id},
        {QStringLiteral("title"), c.title},
        {QStringLiteral("pinned"), c.pinToTop},
        {QStringLiteral("folders"), folders},
    };
}

QVariantMap CollectionStore::openCollectionVariant() const
{
    for (const Collection& c : m_collections) {
        if (c.id == m_openCollection) return collectionVariant(c);
    }
    return {};
}

void CollectionStore::openCollection(const QString& id)
{
    if (m_openCollection == id) return;
    m_openCollection = id;
    emit opened();
}

QList<Collection> CollectionStore::collections() const
{
    return m_collections;
}

QString CollectionStore::createCollection(const QString& title)
{
    const QString clean = title.trimmed();
    if (clean.isEmpty()) return {};
    Collection c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.title = clean;
    m_collections.append(c);
    persist();
    emit changed();
    return c.id;
}

namespace {
Collection* findCollection(QList<Collection>& items, const QString& id)
{
    for (Collection& c : items) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

CollectionFolder* findFolder(Collection& c, const QString& folderId)
{
    for (CollectionFolder& f : c.folders) {
        if (f.id == folderId) return &f;
    }
    return nullptr;
}
} // namespace

void CollectionStore::renameCollection(const QString& id, const QString& title)
{
    const QString clean = title.trimmed();
    if (clean.isEmpty()) return;
    Collection* c = findCollection(m_collections, id);
    if (!c || c->title == clean) return;
    c->title = clean;
    persist();
    emit changed();
}

void CollectionStore::removeCollection(const QString& id)
{
    const int before = m_collections.size();
    m_collections.erase(std::remove_if(m_collections.begin(),
                                       m_collections.end(),
                                       [&](const Collection& c) {
                                           return c.id == id;
                                       }),
                        m_collections.end());
    if (m_collections.size() == before) return;
    persist();
    emit changed();
}

void CollectionStore::moveCollection(const QString& id, int delta)
{
    if (delta == 0) return;
    int at = -1;
    for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections[i].id == id) {
            at = i;
            break;
        }
    }
    if (at < 0) return;
    const int to = std::clamp(at + (delta > 0 ? 1 : -1), 0,
                              int(m_collections.size()) - 1);
    if (to == at) return;
    m_collections.move(at, to);
    persist();
    emit changed();
}

void CollectionStore::setCollectionPinned(const QString& id, bool pinned)
{
    Collection* c = findCollection(m_collections, id);
    if (!c || c->pinToTop == pinned) return;
    c->pinToTop = pinned;
    persist();
    emit changed();
}

QString CollectionStore::createFolder(const QString& collectionId,
                                      const QString& title)
{
    const QString clean = title.trimmed();
    if (clean.isEmpty()) return {};
    Collection* c = findCollection(m_collections, collectionId);
    if (!c) return {};
    CollectionFolder f;
    f.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    f.title = clean;
    c->folders.append(f);
    persist();
    emit changed();
    return f.id;
}

void CollectionStore::renameFolder(const QString& collectionId,
                                   const QString& folderId,
                                   const QString& title)
{
    const QString clean = title.trimmed();
    if (clean.isEmpty()) return;
    Collection* c = findCollection(m_collections, collectionId);
    CollectionFolder* f = c ? findFolder(*c, folderId) : nullptr;
    if (!f || f->title == clean) return;
    f->title = clean;
    persist();
    emit changed();
}

void CollectionStore::removeFolder(const QString& collectionId,
                                   const QString& folderId)
{
    Collection* c = findCollection(m_collections, collectionId);
    if (!c) return;
    const int before = c->folders.size();
    c->folders.erase(std::remove_if(c->folders.begin(), c->folders.end(),
                                    [&](const CollectionFolder& f) {
                                        return f.id == folderId;
                                    }),
                     c->folders.end());
    if (c->folders.size() == before) return;
    persist();
    emit changed();
}

void CollectionStore::addAddonSource(const QString& collectionId,
                                     const QString& folderId,
                                     const QString& addonId,
                                     const QString& type,
                                     const QString& catalogId,
                                     const QString& genre)
{
    if (addonId.isEmpty() || type.isEmpty() || catalogId.isEmpty()) return;
    Collection* c = findCollection(m_collections, collectionId);
    CollectionFolder* f = c ? findFolder(*c, folderId) : nullptr;
    if (!f) return;
    for (const CollectionSource& s : f->addonSources) {
        if (s.addonId == addonId && s.type == type &&
            s.catalogId == catalogId && s.genre == genre.trimmed())
            return;   // already sourced
    }
    CollectionSource s;
    s.addonId = addonId;
    s.type = type;
    s.catalogId = catalogId;
    s.genre = genre.trimmed();
    f->addonSources.append(s);
    persist();
    emit changed();
}

void CollectionStore::removeAddonSource(const QString& collectionId,
                                        const QString& folderId, int index)
{
    Collection* c = findCollection(m_collections, collectionId);
    CollectionFolder* f = c ? findFolder(*c, folderId) : nullptr;
    if (!f || index < 0 || index >= f->addonSources.size()) return;
    f->addonSources.removeAt(index);
    persist();
    emit changed();
}

QString CollectionStore::exportToJson() const
{
    return CollectionCodec::encode(m_collections);
}

void CollectionStore::applyFromRemote(const QString& json)
{
    const QList<Collection> remote = CollectionCodec::decode(json);
    // Empty remote payload clears local state (Compose replace parity);
    // garbage decodes to empty too (runCatching parity) - same observable.
    m_collections = remote;
    persist();
    emit changed();
}

void CollectionStore::load()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("collections"));
    const auto raw = store.getString(profileKey(m_profileId));
    m_collections.clear();
    if (!raw || raw->empty()) return;
    m_collections = CollectionCodec::decode(QString::fromStdString(*raw));
}

namespace {
const Collection* findCollectionConst(const QList<Collection>& items,
                                      const QString& id)
{
    for (const Collection& c : items) {
        if (c.id == id) return &c;
    }
    return nullptr;
}
} // namespace

QVariantList CollectionStore::folderSources() const
{
    QVariantList out;
    const Collection* c = findCollectionConst(m_collections,
                                              m_folderCollection);
    if (!c) return out;
    for (const CollectionFolder& f : c->folders) {
        if (f.id != m_folderId) continue;
        for (const CollectionSource& s : f.addonSources)
            out.append(QVariantMap{
                {QStringLiteral("addonId"), s.addonId},
                {QStringLiteral("type"), s.type},
                {QStringLiteral("catalogId"), s.catalogId},
                {QStringLiteral("genre"), s.genre},
                {QStringLiteral("label"),
                 s.catalogId + (s.genre.isEmpty()
                                    ? QString()
                                    : QStringLiteral(" · ") + s.genre)},
            });
    }
    return out;
}

QVariantList CollectionStore::folderItems() const
{
    if (m_folderSource < 0) return m_folderItems;
    QVariantList out;
    for (const QVariant& v : m_folderItems) {
        if (v.toMap().value(QStringLiteral("sourceIndex"), -2).toInt() ==
            m_folderSource)
            out.append(v);
    }
    return out;
}

QString CollectionStore::folderTitle() const
{
    const Collection* c = findCollectionConst(m_collections,
                                              m_folderCollection);
    if (!c) return {};
    for (const CollectionFolder& f : c->folders) {
        if (f.id == m_folderId) return f.title;
    }
    return {};
}

void CollectionStore::setFolderSourceIndex(int i)
{
    if (m_folderSource == i) return;
    m_folderSource = i;
    emit folderChanged();
}

void CollectionStore::loadFolder(const QString& collectionId,
                                 const QString& folderId)
{
    m_folderCollection = collectionId;
    m_folderId = folderId;
    m_folderSource = -1;
    m_folderItems.clear();
    if (m_openCollection != collectionId) {
        m_openCollection = collectionId;
        emit opened();
    }
    ++m_folderToken;
    const Collection* c = findCollectionConst(m_collections, collectionId);
    if (c) {
        for (const CollectionFolder& f : c->folders) {
            if (f.id != folderId) continue;
            for (const CollectionSource& s : f.addonSources)
                fetchFolderSource(s, m_folderToken);
        }
    }
    emit folderChanged();
}

void CollectionStore::fetchFolderSource(const CollectionSource& source,
                                        quint64 token)
{
    if (!m_registry) return;
    QString base;
    for (const QVariant& rv : m_registry->addons()) {
        const QVariantMap row = rv.toMap();
        if (row.value(QStringLiteral("id")).toString() == source.addonId) {
            base = row.value(QStringLiteral("url")).toString();
            break;
        }
    }
    const QString suffix = QStringLiteral("/manifest.json");
    if (base.endsWith(suffix)) base.chop(suffix.size());
    if (base.isEmpty()) return;
    QString path = base + QStringLiteral("/catalog/") + source.type +
                   QLatin1Char('/') + source.catalogId;
    if (!source.genre.trimmed().isEmpty() &&
        source.genre.compare(QLatin1String("none"),
                             Qt::CaseInsensitive) != 0)
        path += QStringLiteral("/genre=") +
                QString::fromUtf8(
                    QUrl::toPercentEncoding(source.genre.trimmed()));
    path += QStringLiteral(".json");
    QNetworkRequest req{QUrl(path)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, source, token] {
                rep->deleteLater();
                if (token != m_folderToken) return;   // superseded folder
                if (rep->error() != QNetworkReply::NoError) {
                    emit folderChanged();
                    return;
                }
                const QJsonArray metas =
                    QJsonDocument::fromJson(rep->readAll())
                        .object()
                        .value(QLatin1String("metas"))
                        .toArray();
                // Source index = position among this folder's addon
                // sources, recomputed from the live list (stable).
                int index = -1;
                {
                    const Collection* c = findCollectionConst(
                        m_collections, m_folderCollection);
                    if (c) {
                        for (const CollectionFolder& f : c->folders) {
                            if (f.id != m_folderId) continue;
                            for (int i = 0; i < f.addonSources.size();
                                 ++i) {
                                const CollectionSource& s =
                                    f.addonSources[i];
                                if (s.addonId == source.addonId &&
                                    s.type == source.type &&
                                    s.catalogId == source.catalogId &&
                                    s.genre == source.genre)
                                    index = i;
                            }
                        }
                    }
                }
                for (const QJsonValue& mv : metas) {
                    QVariantMap item = CatalogService::itemFromMeta(
                        mv.toObject().toVariantMap(), {});
                    if (!item.isEmpty()) {
                        item.insert(QStringLiteral("sourceIndex"), index);
                        m_folderItems.append(item);
                    }
                }
                emit folderChanged();
            });
}

void CollectionStore::persist()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("collections"));
    store.putString(profileKey(m_profileId),
                    CollectionCodec::encode(m_collections).toStdString());
}

} // namespace nuvio::library
