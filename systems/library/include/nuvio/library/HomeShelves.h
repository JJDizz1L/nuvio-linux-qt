#pragma once

// Home shelves (P4): addon-catalog rails for the home route. Definitions
// derive from ENABLED installed addon manifests (Compose
// buildHomeCatalogDefinitions parity: key manifestId:type:catalogId,
// required-extra catalogs skipped, titles "<name> <Type>"), items fetch
// {transportBase}/catalog/{type}/{id}.json in parallel with a per-refresh
// stale guard. Stored prefs (order/enable/customTitle/hero-source) shape
// the visible sections; hideUnreleasedContent drops YYYY-MM-DD-future
// rows (ISO strings compare lexicographically); heroItems takes the first
// item of up to 2 hero-enabled sections (deterministic; Compose uses a
// seeded random pick - noted divergence).
// Non-tt catalog rows are dropped like everywhere else on this line
// (MetaService detail is tt-only; unopenable cards are worse than none).

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

#include <nuvio/settings/ActiveProfile.h>

#include "nuvio/library/HomeCatalogSettings.h"
#include "nuvio/library/HomeCatalogSync.h"

namespace nuvio::library {

class AddonRegistry;

/// Pure helpers (headless-tested): release filter + hero pick.
[[nodiscard]] QVariantList applyReleaseFilter(const QVariantList& items,
                                              bool hideUnreleased,
                                              const QString& todayIso);
[[nodiscard]] QVariantList pickHeroItems(const QVariantList& sections,
                                         int limit = 2);

class HomeShelves final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)
    Q_PROPERTY(QVariantList heroItems READ heroItems NOTIFY sectionsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY sectionsChanged)
    Q_PROPERTY(QVariantList shelfPrefs READ shelfPrefs NOTIFY prefsChanged)
    Q_PROPERTY(bool heroEnabled READ heroEnabled NOTIFY prefsChanged)
    Q_PROPERTY(bool showCatalogType READ showCatalogType NOTIFY prefsChanged)
    Q_PROPERTY(bool hideUnreleasedContent READ hideUnreleasedContent
                   NOTIFY prefsChanged)

public:
    explicit HomeShelves(AddonRegistry* registry, QObject* parent = nullptr);

    [[nodiscard]] QVariantList sections() const { return m_sections; }
    [[nodiscard]] QVariantList heroItems() const { return m_hero; }
    [[nodiscard]] bool loading() const
    {
        return m_pending > 0 || !m_queue.isEmpty();
    }
    [[nodiscard]] QVariantList shelfPrefs() const;
    [[nodiscard]] bool heroEnabled() const;
    [[nodiscard]] bool showCatalogType() const;
    [[nodiscard]] bool hideUnreleasedContent() const;

    Q_INVOKABLE void refresh();

    /// Profile switches (P7): retargets prefs + refetches.
    Q_INVOKABLE void setProfileId(int profileId);

    // Homescreen settings surface (persist + recompute, no refetch).
    Q_INVOKABLE void setHeroEnabled(bool on);
    Q_INVOKABLE void setShowCatalogType(bool on);
    Q_INVOKABLE void setHideUnreleasedContent(bool on);
    Q_INVOKABLE void setShelfEnabled(const QString& key, bool on);
    Q_INVOKABLE void setShelfHeroSource(const QString& key, bool on);
    Q_INVOKABLE void setShelfCustomTitle(const QString& key,
                                         const QString& title);
    Q_INVOKABLE void moveShelf(const QString& key, int delta);

    /// Sync surface (Appendix A): exportToSyncPayload parity - flags plus
    /// one item per stored row (live definitions supply
    /// addonId/type/catalogId, legacy split covers the rest; hero flags
    /// are local-only and never cross the wire).
    [[nodiscard]] SyncHomeCatalogPayload exportSyncPayload() const;
    /// applyFromRemote parity: flags always apply; a non-empty item list
    /// replaces prefs (local heroSourceEnabled preserved per key, stored
    /// rows for live definitions or explicit sync keys survive when the
    /// remote omits them). Persists + recomputes. Returns whether the
    /// stored payload changed.
    bool applySyncedPayload(const SyncHomeCatalogPayload& remote);

signals:
    void sectionsChanged();
    void prefsChanged();

private:
    struct ShelfData {
        QVariantList items;
        bool done = false;
        bool fetching = false;
    };

    void rebuildDefinitions();
    void recompute();
    void fetchShelf(const QString& key);
    void pumpQueue();

    AddonRegistry* m_registry = nullptr;
    HomeCatalogSettingsStore m_store{
        nuvio::settings::ActiveProfile::id()};
    HomeCatalogPayload m_payload;
    QList<HomeShelfPref> m_rows;          // reconciled, ordered
    QHash<QString, HomeCatalogDefinition> m_defs;  // key -> transport
    QHash<QString, ShelfData> m_data;     // key -> fetched state
    QVariantList m_sections;
    QVariantList m_hero;
    QStringList m_queue;                  // keys awaiting a fetch slot
    QNetworkAccessManager* m_nam = nullptr;
    int m_pending = 0;                    // in-flight fetches (cap below)
    static constexpr int kMaxInFlight = 4;
};

} // namespace nuvio::library
