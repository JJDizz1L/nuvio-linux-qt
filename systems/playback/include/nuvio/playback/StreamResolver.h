#pragma once

// Stream resolution against the Stremio addon protocol (P1 groundwork).
//
// Addons are user-configured in the Compose line; this resolver accepts an
// ordered addon list, derives per-addon stream endpoints from manifest URLs
//   {manifestBase}/stream/{type}/{imdbId}.json
// and applies a STRICT SELECTION POLICY while p2p engines do not exist:
//   1. direct http(s) sources (externalUrl/url) - first match wins
//   2. infoHash-only torrent entries are counted and SKIPPED (they need the
//      P2P engine - plan §8; wiring them before that exists is how phantom
//      playable states happen)
//
// Network-optional by design: networkPath funnels through public
// applyAddonStreams(addonId, body) exactly like CatalogService.ingest - so
// the entire selection contract is offline-testable with fixture bodies.

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QNetworkAccessManager;

namespace nuvio::playback {

struct AddonDescriptor {
    QString id;
    QString name;
    QString manifestUrl;    // absolute https(s) .../manifest.json

    [[nodiscard]] bool valid() const
    { return !id.isEmpty() && !manifestUrl.isEmpty(); }
};

struct ResolvedStream {
    QString source;         // addon id that produced it
    QString title;
    QString url;            // direct-playable when non-empty
    QByteArray infoHash;    // present for torrent entries (skipped for now)
    [[nodiscard]] bool playableDirect() const { return !url.isEmpty(); }
};

class StreamResolver final : public QObject {
    Q_OBJECT
public:
    explicit StreamResolver(QObject* parent = nullptr);

    Q_INVOKABLE void setAddons(const QVariantList& addons); // {id,name,url}
    Q_INVOKABLE QStringList addonIds() const { return m_addonOrder; }

    /// Kick resolution for type+id across all configured addons. Cached
    /// bodies answer synchronously via streamResolved after next event-loop
    /// turn; uncached ones hit the network.
    Q_INVOKABLE void resolve(const QString& type, const QString& imdbId);

    /// Policy application over one addon's raw body (network path shares).
    /// Policy application over one addon body. key = "type/imdbId".
    void applyAddonStreams(const QString& key, const QString& addonId,
                           const QByteArray& body);

    /// Deterministic best-stream pick among CACHED entries. Empty until
    /// every configured addon answered for the key.
    Q_INVOKABLE QVariantMap bestFor(const QString& type,
                                    const QString& imdbId) const;

signals:
    /// Emitted whenever any addon body lands for the key; consumers may
    /// poll bestFor() or track count-completeness via expectedAddons().
    void streamsUpdated(QString type, QString imdbId);
    void resolutionComplete(QString type, QString imdbId,
                            QVariantMap bestStream);

private:
    [[nodiscard]] int  expectedAddons(const QString& key) const;
    [[nodiscard]] int  arrivedCount(const QString& key) const;
    [[nodiscard]] static QVariantMap toVariant(const ResolvedStream& s);
    [[nodiscard]] static ResolvedStream fromEntry(
        const QString& addonId, const QJsonObject& e);

    QStringList                          m_addonOrder;
    QHash<QString, AddonDescriptor>      m_addons;
    // key "type/imdbId" -> per-addon parsed streams
    QHash<QString, QHash<QString, QList<ResolvedStream>>> m_results;

    class TestAccess;                    // offline suite hook

    friend class StreamResolverAccess;   // test hook (offline ingest)
};

} // namespace nuvio::playback