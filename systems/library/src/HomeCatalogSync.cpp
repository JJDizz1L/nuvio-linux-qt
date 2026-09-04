#include "nuvio/library/HomeCatalogSync.h"

#include <QJsonArray>

namespace nuvio::library {

QJsonObject SyncCatalogItem::toJson() const
{
    return QJsonObject{
        {QStringLiteral("addon_id"), addonId},
        {QStringLiteral("type"), type},
        {QStringLiteral("catalog_id"), catalogId},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("order"), order},
        {QStringLiteral("custom_title"), customTitle},
        {QStringLiteral("is_collection"), isCollection},
        {QStringLiteral("collection_id"), collectionId},
        {QStringLiteral("key"), key},
    };
}

SyncCatalogItem SyncCatalogItem::fromJson(const QJsonObject& o)
{
    SyncCatalogItem item;
    item.addonId =
        o.value(QStringLiteral("addon_id")).toString(item.addonId);
    item.type = o.value(QStringLiteral("type")).toString(item.type);
    item.catalogId =
        o.value(QStringLiteral("catalog_id")).toString(item.catalogId);
    item.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    item.order = o.value(QStringLiteral("order")).toInt(0);
    item.customTitle =
        o.value(QStringLiteral("custom_title")).toString(item.customTitle);
    item.isCollection =
        o.value(QStringLiteral("is_collection")).toBool(false);
    item.collectionId = o.value(QStringLiteral("collection_id"))
                            .toString(item.collectionId);
    item.key = o.value(QStringLiteral("key")).toString(item.key);
    return item;
}

QJsonObject SyncHomeCatalogPayload::toJson() const
{
    QJsonArray arr;
    for (const SyncCatalogItem& item : items) arr.append(item.toJson());
    return QJsonObject{
        {QStringLiteral("show_catalog_type"), showCatalogType},
        {QStringLiteral("hide_unreleased_content"), hideUnreleasedContent},
        {QStringLiteral("items"), arr},
    };
}

SyncHomeCatalogPayload SyncHomeCatalogPayload::fromJson(
    const QJsonObject& o, bool localShowCatalogType, bool localHideUnreleased)
{
    SyncHomeCatalogPayload out;
    out.showCatalogType =
        o.contains(QLatin1String("show_catalog_type"))
            ? o.value(QStringLiteral("show_catalog_type"))
                  .toBool(localShowCatalogType)
            : localShowCatalogType;
    out.hideUnreleasedContent =
        o.contains(QLatin1String("hide_unreleased_content"))
            ? o.value(QStringLiteral("hide_unreleased_content"))
                  .toBool(localHideUnreleased)
            : localHideUnreleased;
    for (const QJsonValue& v :
         o.value(QStringLiteral("items")).toArray())
        out.items.append(SyncCatalogItem::fromJson(v.toObject()));
    return out;
}

QJsonObject mergeSyncJson(const QJsonObject& remote, const QJsonObject& local)
{
    QJsonObject out = remote;
    for (auto it = local.constBegin(); it != local.constEnd(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QString preferenceKeyFor(const SyncCatalogItem& item)
{
    if (!item.key.isEmpty()) return item.key;
    if (item.isCollection)
        return QStringLiteral("collection_") + item.collectionId;
    return item.addonId + u':' + item.type + u':' + item.catalogId;
}

bool requiresExplicitSyncKey(const QString& key)
{
    return !key.startsWith(QLatin1String("collection_")) &&
           key.count(u':') > 2;
}

QString addonIdForSyncKey(const QString& key, const QString& type,
                          const QString& catalogId)
{
    const QString suffix =
        u':' + type + u':' + catalogId;
    if (!suffix.isEmpty() && key.endsWith(suffix))
        return key.left(key.size() - suffix.size());
    return key;
}

DecomposedKey decomposeLegacyKey(const QString& key)
{
    DecomposedKey out;
    const QStringList parts = key.split(u':');
    out.addonId = parts.value(0);
    out.type = parts.value(1);
    out.catalogId = parts.value(2);
    return out;
}

} // namespace nuvio::library
