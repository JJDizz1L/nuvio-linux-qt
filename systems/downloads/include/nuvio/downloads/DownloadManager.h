#pragma once

// Downloads domain (A3): verbatim Compose contracts (DownloadItem
// camelCase incl. logicalContentKey/isPlayable/progressFraction rules,
// DownloadStatus names, enqueue replace-by-logical-key, buildFileName
// "<title>[ SxxEyy[ ep]]_<base36now>.<ext>" with sanitized 92-char stem,
// supported-url gate, 3-attempt retry, Range-resume .part files under
// <config>/nuvio-linux/downloads/). Store file "downloads", profile key
// `downloads_<id>` (live Compose convention).

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

namespace nuvio::downloads {

enum class DownloadStatus {
    Downloading,
    Paused,
    Completed,
    Failed,
};

[[nodiscard]] inline QString downloadStatusName(DownloadStatus status)
{
    switch (status) {
    case DownloadStatus::Downloading: return QStringLiteral("Downloading");
    case DownloadStatus::Paused: return QStringLiteral("Paused");
    case DownloadStatus::Completed: return QStringLiteral("Completed");
    case DownloadStatus::Failed: return QStringLiteral("Failed");
    }
    return QStringLiteral("Failed");
}

[[nodiscard]] inline DownloadStatus downloadStatusFromName(const QString& name)
{
    if (name.compare(QLatin1String("Downloading"), Qt::CaseInsensitive) == 0)
        return DownloadStatus::Downloading;
    if (name.compare(QLatin1String("Paused"), Qt::CaseInsensitive) == 0)
        return DownloadStatus::Paused;
    if (name.compare(QLatin1String("Completed"), Qt::CaseInsensitive) == 0)
        return DownloadStatus::Completed;
    return DownloadStatus::Failed;
}

struct DownloadItem {
    QString id;
    QString contentType;
    QString parentMetaId;
    QString parentMetaType;
    QString videoId;
    QString title;
    QString poster;
    int seasonNumber = -1;   // -1 = unset (Compose null)
    int episodeNumber = -1;
    QString episodeTitle;
    QString streamTitle;
    QString providerName;
    QString providerAddonId;
    QString sourceUrl;
    QString localFileUri;
    QString fileName;
    DownloadStatus status = DownloadStatus::Downloading;
    qint64 downloadedBytes = 0;
    qint64 totalBytes = -1;   // -1 = unknown (Compose null)
    QString errorMessage;
    qint64 createdAtEpochMs = 0;
    qint64 updatedAtEpochMs = 0;

    [[nodiscard]] bool isEpisode() const
    {
        return seasonNumber >= 0 && episodeNumber >= 0;
    }
    [[nodiscard]] bool isPlayable() const;
    [[nodiscard]] double progressFraction() const;
    [[nodiscard]] QString logicalContentKey() const;
};

/// Supported-URL gate (magnet/m3u8/mpd/torrent refused, http(s) only).
[[nodiscard]] bool isSupportedDownloadUrl(const QString& url);
/// "<parent>|s|e" or "<parent>|movie" (buildLogicalKey parity).
[[nodiscard]] QString buildLogicalKey(const QString& parentMetaId,
                                      int seasonNumber, int episodeNumber);
/// File stem builder (verbatim rules incl. 92-char cap + base36 stamp).
[[nodiscard]] QString buildFileName(const QString& title, int seasonNumber,
                                    int episodeNumber,
                                    const QString& episodeTitle,
                                    const QString& fallbackTitle,
                                    const QString& sourceUrl,
                                    qint64 nowEpochMs);
[[nodiscard]] QString sanitizeFileName(const QString& raw);
[[nodiscard]] QString fileExtensionFromUrl(const QString& url);

enum class EnqueueResult {
    Started,
    Replaced,
    MissingUrl,
    UnsupportedFormat,
};

class DownloadManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ itemsVariant NOTIFY changed)

public:
    explicit DownloadManager(QObject* parent = nullptr);

    [[nodiscard]] QVariantList itemsVariant() const;
    [[nodiscard]] QList<DownloadItem> items() const;

    /// Enqueues a direct url (replace-by-logical-key semantics).
    /// Returns the outcome + emits changed(); starts downloading.
    Q_INVOKABLE QString enqueue(const QString& contentType,
                                const QString& parentMetaId,
                                const QString& parentMetaType,
                                const QString& videoId, const QString& title,
                                const QString& poster, int seasonNumber,
                                int episodeNumber, const QString& episodeTitle,
                                const QString& streamTitle,
                                const QString& providerName,
                                const QString& sourceUrl);
    Q_INVOKABLE void pauseDownload(const QString& downloadId);
    /// Pauses every in-flight transfer (sign-out/profile-switch parity).
    Q_INVOKABLE void pauseActiveDownloads();
    Q_INVOKABLE void resumeDownload(const QString& downloadId);
    Q_INVOKABLE void cancelDownload(const QString& downloadId);
    /// Opens the downloads folder in the system handler (toast on false).
    Q_INVOKABLE bool openDownloadsDirectory() const;
    /// Local playable file for an identity (next-episode offline path).
    Q_INVOKABLE QString playableLocalFile(const QString& parentMetaId,
                                          int seasonNumber,
                                          int episodeNumber,
                                          const QString& videoId) const;

    /// Profile switches (P7): reloads + aborts in-flight work.
    Q_INVOKABLE void setProfileId(int profileId);

signals:
    void changed();

private:
    struct ActiveTransfer;
    void load();
    void persist();
    void startTransfer(DownloadItem item, int attempt);
    void mutate(const QString& id,
                const std::function<void(DownloadItem&)>& fn);
    [[nodiscard]] QString downloadsDir() const;
    [[nodiscard]] QString nextDownloadId(qint64 nowEpochMs);

    int m_profileId = 1;
    QList<DownloadItem> m_items;
    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, ActiveTransfer*> m_active;
};

} // namespace nuvio::downloads
