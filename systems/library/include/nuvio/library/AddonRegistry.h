#pragma once

// User-configured Stremio addons - P4 blob-parity storage.
//
// TRUTH lives in the Compose `addons.properties` store (AddonStore docs):
// installed_addon_urls_1 + addon_enabled_states_1, so both builds see one
// install set. Manifest BODIES are an optimization cache (qt-addons,
// sha256(url) keys); missing bodies are refetched asynchronously and rows
// appear as URL placeholders until arrival (never blocking the QML thread).
//
// Rows expose {url, id, name, types[], enabled}; id is "" while the manifest
// is still being fetched. remove() keys on id (the registry's UI contract).

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

#include "nuvio/library/AddonStore.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace nuvio::settings {
class PropertiesStore;
}

namespace nuvio::library {

class AddonRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList addons READ addons NOTIFY changed)

public:
    explicit AddonRegistry(QObject* parent = nullptr);
    /// Defined out-of-line: unique_ptr members over a fwd-declared store.
    ~AddonRegistry() override;

    /// Reconciles Compose truth URLs with cached manifests at construction-
    /// time call; refetches anything uncached async (changed() per arrival).
    Q_INVOKABLE void load();

    /// Normalizes like Compose, dedupes, fetches + validates the manifest,
    /// then persists truth+enabled(true)+cache. Emits addResult(ok, message).
    Q_INVOKABLE void add(const QString& manifestUrl);
    Q_INVOKABLE void remove(const QString& id);
    /// Enabled-state toggle persisted to the shared truth store.
    Q_INVOKABLE void setEnabled(int index, bool on);

    [[nodiscard]] QVariantList addons() const { return m_addons; }

    /// Pure manifest validation/mapping - unit-tested without network.
    [[nodiscard]] static QVariantMap parseManifest(const QString& url,
                                                   const QByteArray& body);

signals:
    void changed();
    void addResult(bool ok, QString message);
    void removed(QString id);

private:
    void fetchManifest(const QString& url);   // async; updates row + cache
    void persistTruth();
    void rebuildRow(const QString& url, const QByteArray& body);

    QVariantList m_addons;
    // Single long-lived store instances: PropertiesStore snapshots at
    // construction, so per-call instances would clobber each other's writes.
    std::unique_ptr<nuvio::settings::PropertiesStore> m_truth;
    std::unique_ptr<nuvio::settings::PropertiesStore> m_cache;
    class  QNetworkAccessManager* m_nam = nullptr;
};

} // namespace nuvio::library