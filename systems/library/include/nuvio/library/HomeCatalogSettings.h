#pragma once

// Home catalog settings (P4): verbatim port of Compose's
// home_catalog_settings payload (StoredHomeCatalogSettingsPayload:
// heroEnabled/showCatalogType/hideUnreleasedContent/items[{key,customTitle,
// enabled,heroSourceEnabled,order}], kotlinx encodeDefaults +
// ignoreUnknownKeys). Store file "home_catalog_settings", profile-scoped
// key (live-proven shape). Reconcile merges live addon-catalog definitions
// with stored prefs: unknown keys append with defaults, stored rows for
// vanished addons are kept (they resurface if the addon returns) but never
// displayed.

#include <QList>
#include <QObject>
#include <QString>

#include <nuvio/settings/ActiveProfile.h>

namespace nuvio::library {

struct HomeShelfPref {
    QString key;
    QString defaultTitle;
    QString addonName;
    QString customTitle;
    bool enabled = true;
    bool heroSourceEnabled = true;
    int order = 0;

    [[nodiscard]] QString displayTitle(bool showCatalogType) const
    {
        if (!customTitle.isEmpty()) return customTitle;
        if (showCatalogType) return defaultTitle;
        const int cut = defaultTitle.lastIndexOf(u' ');
        return cut > 0 ? defaultTitle.left(cut) : defaultTitle;
    }
};

struct HomeCatalogPayload {
    bool heroEnabled = true;
    bool showCatalogType = true;
    bool hideUnreleasedContent = false;
    QList<HomeShelfPref> items;
};

class HomeCatalogSettingsCodec final {
public:
    [[nodiscard]] static HomeCatalogPayload decode(const QString& json);
    [[nodiscard]] static QString encode(const HomeCatalogPayload& payload);
};

/// Live addon-catalog definition feeding reconcile().
struct HomeCatalogDefinition {
    QString key;            // manifestId:type:catalogId
    QString defaultTitle;   // "<name> <Type>" (Compose defaultTitle parity)
    QString addonName;
    QString addonId;
    QString type;
    QString catalogId;
    QString transportBase;  // manifest url minus /manifest.json
};

class HomeCatalogSettingsStore final {
public:
    explicit HomeCatalogSettingsStore(
        int profileId = nuvio::settings::ActiveProfile::id());

    /// Profile switches (P7).
    void setProfileId(int profileId) { m_profileId = profileId; }

    [[nodiscard]] HomeCatalogPayload load() const;
    void save(const HomeCatalogPayload& payload);

    /// Merges definitions with stored prefs (persists only when new keys
    /// appear). Returns the full ordered row set, stored flags applied.
    [[nodiscard]] QList<HomeShelfPref> reconcile(
        const QList<HomeCatalogDefinition>& definitions);

private:
    int m_profileId;
};

} // namespace nuvio::library
