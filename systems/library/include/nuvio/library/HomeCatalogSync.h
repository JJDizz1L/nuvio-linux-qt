#pragma once

// Home-catalog sync codec (Appendix A, home-catalog settings sync): verbatim
// port of Compose's SyncCatalogItem / SyncHomeCatalogPayload wire shapes
// (snake_case JSON, encodeDefaults: every field always written) plus the
// pure merge/key helpers from HomeCatalogSettingsSyncService and
// HomeCatalogSettingsRepository:
//   - mergeSyncJson: remote entries first, LOCAL wins per key
//     (mergeHomeCatalogSettingsJson parity).
//   - preferenceKeyFor: item.key when present, else collection_<id> for
//     collections, else addonId:type:catalogId.
//   - requiresExplicitSyncKey: preserved-across-applies even when unknown:
//     non-collection keys with more than two colons.
//   - addonIdForSyncKey: key minus the ":type:catalogId" suffix (manifest
//     ids may themselves contain colons; a bare split would corrupt them).
//   - decomposeLegacyKey: "a:b:c" split fallback when no live definition
//     is known (exportToSyncPayload parity).
// Platform + RPC names mirror SyncPlatform / the sync service.

#include <QJsonObject>
#include <QList>
#include <QString>

namespace nuvio::library {

inline constexpr char kHomeCatalogSyncPlatform[] = "home_catalog_shared";
inline constexpr char kHomeCatalogPullRpc[] =
    "sync_pull_home_catalog_settings";
inline constexpr char kHomeCatalogPushRpc[] =
    "sync_push_home_catalog_settings";
inline constexpr char kHomeCatalogSettingsJsonKey[] = "settings_json";

struct SyncCatalogItem {
    QString addonId;
    QString type;
    QString catalogId;
    bool enabled = true;
    int order = 0;
    QString customTitle;
    bool isCollection = false;
    QString collectionId;
    QString key;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static SyncCatalogItem fromJson(const QJsonObject& o);
};

struct SyncHomeCatalogPayload {
    bool showCatalogType = true;
    bool hideUnreleasedContent = false;
    QList<SyncCatalogItem> items;

    [[nodiscard]] QJsonObject toJson() const;
    /// Tolerant decode (kotlinx ignoreUnknownKeys parity): garbage yields
    /// defaults. Missing top-level flags fall back to the LOCAL values
    /// passed in (decodePayloadPreservingLocalDefaults parity - encodeDefaults
    /// always writes them, so absent only happens on foreign/hand edits).
    [[nodiscard]] static SyncHomeCatalogPayload fromJson(
        const QJsonObject& o, bool localShowCatalogType,
        bool localHideUnreleased);
};

/// Remote entries first, local entries overwrite (top-level per-key merge).
[[nodiscard]] QJsonObject mergeSyncJson(const QJsonObject& remote,
                                       const QJsonObject& local);

/// Preference key the item syncs under (SyncCatalogItem.preferenceKey).
[[nodiscard]] QString preferenceKeyFor(const SyncCatalogItem& item);

/// Unknown keys still preserved across a remote apply.
[[nodiscard]] bool requiresExplicitSyncKey(const QString& key);

/// Manifest id for sync: key minus the trailing ":type:catalogId".
[[nodiscard]] QString addonIdForSyncKey(const QString& key,
                                       const QString& type,
                                       const QString& catalogId);

/// Legacy "addonId:type:catalogId" split (limit 3) for rows with no live
/// definition (exportToSyncPayload fallback parity).
struct DecomposedKey {
    QString addonId;
    QString type;
    QString catalogId;
};
[[nodiscard]] DecomposedKey decomposeLegacyKey(const QString& key);

} // namespace nuvio::library
