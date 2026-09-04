#pragma once

// MDBList ratings settings (fork features/mdblist/MdbListSettings{,
// Repository,Storage} parity): 10 verbatim `mdblist_*` keys,
// profile-scoped, enabled&&apiKey gate, blank-key disables, per-id
// provider toggles (unknown ids ignored). The API key is a credential:
// stored locally, stripped from the sync blob, traveling only through
// the provider-credentials family (Qt T4 leg).

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace nuvio::mdblist {

class MdbListSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY changed)
    Q_PROPERTY(bool useImdb READ useImdb NOTIFY changed)
    Q_PROPERTY(bool useTmdb READ useTmdb NOTIFY changed)
    Q_PROPERTY(bool useTomatoes READ useTomatoes NOTIFY changed)
    Q_PROPERTY(bool useMetacritic READ useMetacritic NOTIFY changed)
    Q_PROPERTY(bool useTrakt READ useTrakt NOTIFY changed)
    Q_PROPERTY(bool useLetterboxd READ useLetterboxd NOTIFY changed)
    Q_PROPERTY(bool useAudience READ useAudience NOTIFY changed)
    Q_PROPERTY(bool useMal READ useMal NOTIFY changed)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY changed)

public:
    explicit MdbListSettings(QObject* parent = nullptr);

    [[nodiscard]] bool enabled() const { return m_enabled; }
    [[nodiscard]] QString apiKey() const { return m_apiKey; }
    [[nodiscard]] bool useImdb() const { return m_useImdb; }
    [[nodiscard]] bool useTmdb() const { return m_useTmdb; }
    [[nodiscard]] bool useTomatoes() const { return m_useTomatoes; }
    [[nodiscard]] bool useMetacritic() const { return m_useMetacritic; }
    [[nodiscard]] bool useTrakt() const { return m_useTrakt; }
    [[nodiscard]] bool useLetterboxd() const { return m_useLetterboxd; }
    [[nodiscard]] bool useAudience() const { return m_useAudience; }
    [[nodiscard]] bool useMal() const { return m_useMal; }
    [[nodiscard]] bool hasApiKey() const { return !m_apiKey.isEmpty(); }

    /// Per-provider toggle (unknown ids ignored, fork parity).
    [[nodiscard]] bool isProviderEnabled(const QString& providerId) const;
    /// Enabled providers in fetch priority order.
    [[nodiscard]] QStringList enabledProviders() const;

    Q_INVOKABLE void setEnabled(bool value);
    Q_INVOKABLE void setApiKey(const QString& value);
    Q_INVOKABLE void setProviderEnabled(const QString& providerId,
                                        bool value);

    /// Profile switches (P7): reloads from the new profile's keys.
    Q_INVOKABLE void setProfileId(int profileId);
    /// Forces a reload (sync-leg merges write the store directly).
    Q_INVOKABLE void reload();

    /// Sync-blob owned feature (present-only envelopes on export, key
    /// stripped by credential policy at assembly in the orchestrator).
    [[nodiscard]] QJsonObject exportSyncPayload() const;
    /// Per-key merge (absent untouched, Qt debrid convention); the api
    /// key applies when present (stored, never pushed back).
    /// Returns true when anything changed.
    bool applySyncPayload(const QJsonObject& payload);

signals:
    void changed();

private:
    void load();
    void publish();
    [[nodiscard]] QString scoped(const char* key) const;

    int m_profileId = 1;
    bool m_enabled = false;
    QString m_apiKey;
    bool m_useImdb = true;
    bool m_useTmdb = true;
    bool m_useTomatoes = true;
    bool m_useMetacritic = true;
    bool m_useTrakt = true;
    bool m_useLetterboxd = true;
    bool m_useAudience = true;
    bool m_useMal = true;
    bool m_loaded = false;
};

} // namespace nuvio::mdblist
