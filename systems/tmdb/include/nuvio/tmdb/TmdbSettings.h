#pragma once

// TMDB settings (fork features/tmdb/TmdbSettings{,Repository,Storage}
// parity): 15 verbatim `tmdb_*` keys, profile-scoped, enabled&&apiKey
// gate, blank-key disables, `_`->`-` language normalization. The API
// key is a credential: stored locally, stripped from the sync blob,
// traveling only through the provider-credentials family (Qt T4 leg).
// The use* module switches persist + sync; they gate the metadata
// enrichment engine, which stays deferred per P6 (honest inert flags).

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace nuvio::tmdb {

[[nodiscard]] QString normalizeLanguage(const QString& value);

class TmdbSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY changed)
    Q_PROPERTY(QString language READ language NOTIFY changed)
    Q_PROPERTY(bool useTrailers READ useTrailers NOTIFY changed)
    Q_PROPERTY(bool useArtwork READ useArtwork NOTIFY changed)
    Q_PROPERTY(bool useBasicInfo READ useBasicInfo NOTIFY changed)
    Q_PROPERTY(bool useDetails READ useDetails NOTIFY changed)
    Q_PROPERTY(bool useReleaseDates READ useReleaseDates NOTIFY changed)
    Q_PROPERTY(bool useCredits READ useCredits NOTIFY changed)
    Q_PROPERTY(bool useProductions READ useProductions NOTIFY changed)
    Q_PROPERTY(bool useNetworks READ useNetworks NOTIFY changed)
    Q_PROPERTY(bool useEpisodes READ useEpisodes NOTIFY changed)
    Q_PROPERTY(bool useSeasonPosters READ useSeasonPosters NOTIFY changed)
    Q_PROPERTY(bool useMoreLikeThis READ useMoreLikeThis NOTIFY changed)
    Q_PROPERTY(bool useCollections READ useCollections NOTIFY changed)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY changed)

public:
    explicit TmdbSettings(QObject* parent = nullptr);

    [[nodiscard]] bool enabled() const { return m_enabled; }
    [[nodiscard]] QString apiKey() const { return m_apiKey; }
    [[nodiscard]] QString language() const { return m_language; }
    [[nodiscard]] bool useTrailers() const { return m_useTrailers; }
    [[nodiscard]] bool useArtwork() const { return m_useArtwork; }
    [[nodiscard]] bool useBasicInfo() const { return m_useBasicInfo; }
    [[nodiscard]] bool useDetails() const { return m_useDetails; }
    [[nodiscard]] bool useReleaseDates() const { return m_useReleaseDates; }
    [[nodiscard]] bool useCredits() const { return m_useCredits; }
    [[nodiscard]] bool useProductions() const { return m_useProductions; }
    [[nodiscard]] bool useNetworks() const { return m_useNetworks; }
    [[nodiscard]] bool useEpisodes() const { return m_useEpisodes; }
    [[nodiscard]] bool useSeasonPosters() const { return m_useSeasonPosters; }
    [[nodiscard]] bool useMoreLikeThis() const { return m_useMoreLikeThis; }
    [[nodiscard]] bool useCollections() const { return m_useCollections; }
    [[nodiscard]] bool hasApiKey() const { return !m_apiKey.isEmpty(); }

    Q_INVOKABLE void setEnabled(bool value);
    Q_INVOKABLE void setApiKey(const QString& value);
    Q_INVOKABLE void setLanguage(const QString& value);
    Q_INVOKABLE void setUseTrailers(bool value);
    Q_INVOKABLE void setUseArtwork(bool value);
    Q_INVOKABLE void setUseBasicInfo(bool value);
    Q_INVOKABLE void setUseDetails(bool value);
    Q_INVOKABLE void setUseReleaseDates(bool value);
    Q_INVOKABLE void setUseCredits(bool value);
    Q_INVOKABLE void setUseProductions(bool value);
    Q_INVOKABLE void setUseNetworks(bool value);
    Q_INVOKABLE void setUseEpisodes(bool value);
    Q_INVOKABLE void setUseSeasonPosters(bool value);
    Q_INVOKABLE void setUseMoreLikeThis(bool value);
    Q_INVOKABLE void setUseCollections(bool value);

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
    QString m_language = QStringLiteral("en");
    bool m_useTrailers = true;
    bool m_useArtwork = true;
    bool m_useBasicInfo = true;
    bool m_useDetails = true;
    bool m_useReleaseDates = false;
    bool m_useCredits = true;
    bool m_useProductions = true;
    bool m_useNetworks = true;
    bool m_useEpisodes = true;
    bool m_useSeasonPosters = true;
    bool m_useMoreLikeThis = true;
    bool m_useCollections = true;
    bool m_loaded = false;
};

} // namespace nuvio::tmdb
