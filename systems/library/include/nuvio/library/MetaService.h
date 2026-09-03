#pragma once

// Detail-page metadata against Cinemeta ({base}/meta/{type}/{imdbId}.json).
//
// Contract notes (mirroring CatalogService):
//  * URL: {base}/meta/{type}/{imdbId}.json - base defaults to
//    https://v3-cinemeta.strem.io, overridable via NUVIO_CINEMETA_BASE
//    (read at construction; nothing machine-specific in source).
//  * One instance carries ONE current payload ("current"); the caller
//    navigates to the meta route right after load() and binds to this
//    object. Re-entering the same card re-fetches unless the key matches
//    (cheap client-side cache for the common browse-back case).
//  * While loading, "current" is immediately populated with at least the
//    requested id/title (+previous rich fields when the card is already
//    cached) so the page never flashes empty.
//  * Episode lists are normalized to {season,episode,name,description,
//    thumb} with BOTH known Cinemeta video-id shapes parsed defensively:
//      modern  tt12345:1:4      legacy tt12345::season:1:episode:4
//    sorted ascending by (season, episode); malformed entries dropped +
//    counted like everywhere else - silent data loss is the enemy.
//  * Malformed bodies resolve to an EMPTY current + lastError text, never
//    a partially populated model.

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;

namespace nuvio::library {

class MetaService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap current   READ current   NOTIFY currentChanged)
    Q_PROPERTY(bool         loading  READ loading  NOTIFY loadingChanged)
    Q_PROPERTY(QString      lastError READ lastError NOTIFY lastErrorChanged)
    /// Episode-list presentation (Compose SeasonViewMode parity:
    /// "posters"|"text", persisted profile-scoped).
    Q_PROPERTY(QString seasonViewMode READ seasonViewMode
                   WRITE setSeasonViewMode NOTIFY seasonViewModeChanged)

public:
    explicit MetaService(QObject* parent = nullptr);

    /// Returns quickly; results arrive via currentChanged.
    /// displayName seeds the UI until the network answer lands.
    Q_INVOKABLE void load(const QString& type, const QString& imdbId,
                          const QString& displayName = QString());
    Q_INVOKABLE void toggleSeasonViewMode();

    [[nodiscard]] QString seasonViewMode() const;
    void setSeasonViewMode(const QString& mode);

    [[nodiscard]] QVariantMap current() const { return m_current; }
    [[nodiscard]] bool        loading() const { return m_loading; }
    [[nodiscard]] QString     lastError() const { return m_lastError; }

    /// Pure normalization of one meta body -> flat map as documented in the
    /// header comment above. Empty map on malformed input. Offline-testable.
    [[nodiscard]] static QVariantMap metaFromJson(const QByteArray& body);

signals:
    void currentChanged();
    void loadingChanged();
    void lastErrorChanged();
    void seasonViewModeChanged();

private:
    void publish(const QVariantMap& map);
    void setLoading(bool v);

    QVariantMap            m_current;
    bool                   m_loading = false;
    QString                m_lastError;
    QString                m_loadedKey;      // "type/imdbId" of m_current
    QByteArray             m_baseUrl;
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace nuvio::library