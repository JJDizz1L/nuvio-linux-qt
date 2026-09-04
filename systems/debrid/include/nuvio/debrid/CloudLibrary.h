#pragma once

// Cloud library (D3): verbatim port of Compose's cloud mapping rules
// (Torbox torrent/usenet/webdl lists, Premiumize listall grouping,
// playable-extension tables, name fallbacks, progress fractions) plus a
// thin async fetcher over DebridSettings keys. Playback resolves per
// item/file through the same request-download endpoints as D2.

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace nuvio::debrid {

class DebridSettings;

/// Cloud playable-video extensions (Torbox + Premiumize tables are
/// identical verbatim; kept as one set deliberately).
[[nodiscard]] bool cloudFilePlayable(const QString& name,
                                     const QString& mimeType);

struct CloudLibraryFile {
    QString id;
    QString name;
    qint64 sizeBytes = -1;
    QString mimeType;
    bool playable = false;
    QString playbackUrl;   // premiumize listall links ride along
};

struct CloudLibraryItem {
    QString providerId;
    QString providerName;
    QString id;
    QString type;   // Torrent | Usenet | WebDownload | File
    QString name;
    QString status;
    qint64 sizeBytes = -1;
    double progressFraction = -1.0;   // <0 when unknown
    QList<CloudLibraryFile> files;
};

/// Pure mappers (headless-tested): provider list bodies -> items.
[[nodiscard]] QList<CloudLibraryItem> parseTorboxCloudList(
    const QByteArray& body, const QString& type);
[[nodiscard]] QList<CloudLibraryItem> parsePremiumizeCloudList(
    const QByteArray& body);

class CloudLibrary final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ itemsVariant NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)

public:
    explicit CloudLibrary(DebridSettings* settings,
                          QObject* parent = nullptr);

    [[nodiscard]] QVariantList itemsVariant() const;
    [[nodiscard]] bool loading() const { return m_pending > 0; }
    [[nodiscard]] QString errorMessage() const { return m_error; }

    Q_INVOKABLE void refresh();
    /// Resolves a playable file to a url (torbox requestdl per type,
    /// premiumize playbackUrl-or-details). Exactly one signal per call.
    Q_INVOKABLE void resolvePlayback(const QString& providerId,
                                     const QString& itemId,
                                     const QString& itemType,
                                     const QString& fileId);

signals:
    void changed();
    void playbackResolved(const QString& url, const QString& filename);
    void playbackFailed(const QString& message);

private:
    void ingest(const QString& providerId, QList<CloudLibraryItem> items);

    DebridSettings* m_settings = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QList<CloudLibraryItem> m_items;
    QString m_error;
    int m_pending = 0;
    quint64 m_token = 0;
};

} // namespace nuvio::debrid
