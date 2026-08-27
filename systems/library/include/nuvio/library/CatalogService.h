#pragma once

// Catalog federation against Stremio's public Cinemeta addon (keyless).
//
// Contract notes:
//  * URL: {base}/catalog/{type}/{catalog}.json - base defaults to
//    https://v3-cinemeta.strem.io, overridable for tests/mirrors via env
//    NUVIO_CINEMETA_BASE (read at construction; never hardcoded per-user
//    values in source).
//  * Item posters: use the manifest-provided absolute URL when http(s);
//    otherwise build the canonical metahub poster from the imdb id so both
//    response shapes render identically.
//  * Malformed entries are DROPPED (never null-prop into the UI model) but
//    always counted + logged - silent data loss is the enemy.
//
// Results arrive as QVariantList of QVariantMaps with stable keys:
//   id | name | year | poster   (poster already resolved absolute)

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;

namespace nuvio::library {

class CatalogService final : public QObject {
    Q_OBJECT
public:
    explicit CatalogService(QObject* parent = nullptr);

    /// Emits catalogReady(type, catalogId, items) on completion (success or
    /// empty-with-error; error text rides lastError()).
    Q_INVOKABLE void fetch(const QString& type, const QString& catalogId);

    [[nodiscard]] QString lastError() const { return m_lastError; }

    // ---- multi-shelf container -------------------------------------------
    // Order follows request order; a shelf stays visible with empty items
    // while pending/failed so headers never jump around.
    Q_PROPERTY(QVariantList shelves READ shelves NOTIFY shelvesChanged)
    [[nodiscard]] QVariantList shelves() const { return m_shelves; }

    /// Standard P1 rail set.
    Q_INVOKABLE void loadShelves();

    /// Parse an already-fetched body into the named shelf and emit
    /// shelvesChanged. Public so the offline suite can drive parsing
    /// deterministically (network path funnels through here too).
    Q_INVOKABLE void ingest(const QString& type, const QString& catalogId,
                            const QByteArray& body);

    /// Pure mapping helper - unit-tested directly without network.
    [[nodiscard]] static QVariantMap itemFromMeta(const QVariantMap& meta,
                                                  const QByteArray& baseUrl);

signals:
    void catalogReady(QString type, QString catalogId, QVariantList items);
    void shelvesChanged();

private:
    void handleReply(const QString& type, const QString& catalogId,
                     const QByteArray& body);
    [[nodiscard]] static QString shelfTitle(const QString& type,
                                            const QString& catalogId);
    [[nodiscard]] int  indexOfShelf(const QString& type,
                                    const QString& catalogId) const;
    static QVariantMap newShelf(const QString& type,
                                const QString& catalogId);
    QVariantList       m_shelves;
    QStringList        m_order;   // "type/catalogId" in display order

    QNetworkAccessManager* m_nam = nullptr;
    QByteArray m_baseUrl;
    QString    m_lastError;
};

} // namespace nuvio::library