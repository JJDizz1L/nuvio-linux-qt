#pragma once

// User-configured Stremio addons (manifest URLs -> identity+name+types).
//
// STORAGE DIVERGENCE - deliberate and documented (plan §4 L1):
// The Compose line persists addon installs inside its Supabase sync blob
// schema (P4/authsync contract zone). Until that contract is ported, THIS
// line owns an isolated file (qt-addons.properties) so no invented key can
// ever collide with upstream profile data. Migration = P4 work item.
//
// File layout: keys "addon_<n>" ascending, value = JSON object
//   {"url":..., "id":..., "name":..., "types":[...]}
// Removal compacts indices (stable, atomic tmp-rename persistence).

#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;

namespace nuvio::library {

class AddonRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList addons READ addons NOTIFY changed)

public:
    explicit AddonRegistry(QObject* parent = nullptr);

    /// Reads qt-addons.properties (if present) at construction-time call.
    Q_INVOKABLE void load();

    /// Fetches <url> (adds .json if missing suffix), validates the manifest,
    /// dedupes by id/url, persists. Emits addResult(ok, message).
    Q_INVOKABLE void add(const QString& manifestUrl);
    Q_INVOKABLE void remove(const QString& id);

    [[nodiscard]] QVariantList addons() const { return m_addons; }

    /// Pure manifest validation/mapping - unit-tested without network.
    [[nodiscard]] static QVariantMap parseManifest(const QString& url,
                                                   const QByteArray& body);

signals:
    void changed();
    void addResult(bool ok, QString message);
    void removed(QString id);

private:
    void persist();
    void finishAdd(const QString& normalizedUrl, const QByteArray& body);

    QVariantList m_addons;
    class  QNetworkAccessManager* m_nam = nullptr;
};

} // namespace nuvio::library