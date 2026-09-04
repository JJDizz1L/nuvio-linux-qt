#pragma once

// Torrent-to-direct resolution (D2): verbatim Compose DirectDebridResolver
// torrent flow (credential pick → cache check → create → file select →
// download link) over info-hash torrents. Torbox and Premiumize resolve;
// Real-Debrid never auto-resolves (invisible in configured services,
// Compose parity). Results are single-flight with a pending-key guard.
// Magnets build as btih:hash + dn=title (trackers unknown on this line).

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace nuvio::debrid {

class DebridSettings;

/// Preferred-or-first configured VISIBLE provider id with a stored key
/// ("" when none can resolve). Verbatim preferredResolverService rule.
[[nodiscard]] QString activeResolverProviderId(const DebridSettings& settings);
[[nodiscard]] QString magnetForHash(const QString& infoHashHex,
                                    const QString& title);

/// Premiumize directdl error classification (cache/not-found message ->
/// NotCached, else Stale; 401/403 -> Error). Verbatim resolver rule.
enum class PremiumizeFailure {
    NotCached,
    Stale,
    Error,
};
[[nodiscard]] PremiumizeFailure classifyPremiumizeError(
    int httpStatus, const QString& messageLower);

class DebridResolver final : public QObject {
    Q_OBJECT

public:
    explicit DebridResolver(DebridSettings* settings,
                            QObject* parent = nullptr);

    /// Test hook (also mirror-friendly): overrides the API origin per
    /// provider id. Empty restores production hosts.
    void setEndpointOverride(const QString& providerId, const QString& baseUrl);

    /// True when debrid is enabled and a resolver credential exists
    /// (Compose canResolvePlayableLinks parity).
    [[nodiscard]] bool canResolve() const;

    /// Resolves a torrent info-hash to a playable url. Exactly one of
    /// resolved()/unavailable() fires per call (superseded calls stay
    /// silent via the pending-key guard).
    Q_INVOKABLE void resolveTorrent(const QString& infoHashHex,
                                    const QString& title, int season = -1,
                                    int episode = -1);

signals:
    /// Direct playable url (+ filename + provider id for attribution).
    void resolved(const QString& url, const QString& filename,
                  const QString& providerId);
    /// Honest negatives: NoCredential | NotCached | Stale | Error.
    void unavailable(const QString& reason);

private:
    void resolveTorbox(const QString& apiKey, const QString& magnet,
                       const QString& title, int season, int episode,
                       quint64 token);
    void resolvePremiumize(const QString& apiKey, const QString& magnet,
                           const QString& title, int season, int episode,
                           quint64 token);
    [[nodiscard]] QString torboxUrl(const QString& path) const;
    [[nodiscard]] QString premiumizeUrl(const QString& path) const;
    void finishOk(const QString& url, const QString& filename,
                  const QString& providerId, quint64 token);
    void finishFail(const QString& reason, quint64 token);

    DebridSettings* m_settings = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, QString> m_baseOverrides;
    quint64 m_token = 0;
    QString m_pendingKey;
};

} // namespace nuvio::debrid
