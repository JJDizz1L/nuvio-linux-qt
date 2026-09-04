#include "nuvio/downloads/DownloadManager.h"

#include <algorithm>
#include <atomic>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include "nuvio/platform/XdgPaths.h"
#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::downloads {

namespace {
constexpr int kMaxAttempts = 3;   // Compose MaxDownloadAttempts parity

[[nodiscard]] QString profileKey(int profileId)
{
    return QStringLiteral("downloads_") + QString::number(profileId);
}
} // namespace

bool DownloadItem::isPlayable() const
{
    return status == DownloadStatus::Completed && !localFileUri.isEmpty();
}

double DownloadItem::progressFraction() const
{
    if (totalBytes <= 0) return 0.0;
    return std::clamp(double(downloadedBytes) / double(totalBytes), 0.0, 1.0);
}

QString DownloadItem::logicalContentKey() const
{
    return buildLogicalKey(parentMetaId, seasonNumber, episodeNumber);
}

bool isSupportedDownloadUrl(const QString& url)
{
    const QString n = url.trimmed().toLower();
    if (n.startsWith(QLatin1String("magnet:"))) return false;
    if (n.endsWith(QLatin1String(".m3u8")) || n.contains(QLatin1String(".m3u8?")))
        return false;
    if (n.endsWith(QLatin1String(".mpd")) || n.contains(QLatin1String(".mpd?")))
        return false;
    if (n.endsWith(QLatin1String(".torrent")) ||
        n.contains(QLatin1String(".torrent?")))
        return false;
    return n.startsWith(QLatin1String("http://")) ||
           n.startsWith(QLatin1String("https://"));
}

QString buildLogicalKey(const QString& parentMetaId, int seasonNumber,
                        int episodeNumber)
{
    if (seasonNumber >= 0 && episodeNumber >= 0)
        return parentMetaId.trimmed() + u'|' + QString::number(seasonNumber) +
               u'|' + QString::number(episodeNumber);
    return parentMetaId.trimmed() + QStringLiteral("|movie");
}

QString sanitizeFileName(const QString& raw)
{
    QString out = raw.trimmed();
    for (QChar& c : out) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                        (u >= '0' && u <= '9') || u == '.' || u == '_' ||
                        u == ' ' || u == '-';
        if (!ok) c = u'_';
    }
    return out;
}

QString fileExtensionFromUrl(const QString& url)
{
    QString noQuery = url;
    const int q = noQuery.indexOf(u'?');
    if (q >= 0) noQuery.truncate(q);
    const int h = noQuery.indexOf(u'#');
    if (h >= 0) noQuery.truncate(h);
    const int dot = noQuery.lastIndexOf(u'.');
    if (dot < 0) return QStringLiteral("mp4");
    const QString suffix = noQuery.mid(dot + 1).toLower().trimmed();
    if (suffix.size() < 2 || suffix.size() > 5) return QStringLiteral("mp4");
    for (const QChar c : suffix) {
        if (!c.isLetterOrNumber()) return QStringLiteral("mp4");
    }
    return suffix;
}

QString buildFileName(const QString& title, int seasonNumber,
                      int episodeNumber, const QString& episodeTitle,
                      const QString& fallbackTitle, const QString& sourceUrl,
                      qint64 nowEpochMs)
{
    QString base;
    if (seasonNumber >= 0 && episodeNumber >= 0) {
        base = title + u' ' + u'S' +
               QString::number(seasonNumber).rightJustified(2, u'0') + u'E' +
               QString::number(episodeNumber).rightJustified(2, u'0');
        if (!episodeTitle.trimmed().isEmpty()) base += u' ' + episodeTitle;
    } else {
        base = title.trimmed().isEmpty() ? fallbackTitle : title;
    }
    QString stem = sanitizeFileName(base);
    if (stem.isEmpty()) stem = QStringLiteral("download");
    stem = stem.left(92);
    return stem + u'_' + QString::number(nowEpochMs, 36) + u'.' +
           fileExtensionFromUrl(sourceUrl);
}

struct DownloadManager::ActiveTransfer {
    QNetworkReply* reply = nullptr;
    QFile* file = nullptr;
    qint64 startedBytes = 0;
    int attempt = 1;
    QString id;
};

DownloadManager::DownloadManager(QObject* parent)
    : QObject(parent),
      m_profileId(nuvio::settings::ActiveProfile::id()),
      m_nam(new QNetworkAccessManager(this))
{
    load();
}

QString DownloadManager::downloadsDir() const
{
    const QString dir =
        nuvio::platform::appConfigDir() + QStringLiteral("/downloads");
    QDir().mkpath(dir);
    return dir;
}

QVariantList DownloadManager::itemsVariant() const
{
    QVariantList out;
    for (const DownloadItem& it : m_items)
        out.append(QVariantMap{
            {QStringLiteral("id"), it.id},
            {QStringLiteral("contentType"), it.contentType},
            {QStringLiteral("parentMetaId"), it.parentMetaId},
            {QStringLiteral("videoId"), it.videoId},
            {QStringLiteral("title"), it.title},
            {QStringLiteral("poster"), it.poster},
            {QStringLiteral("seasonNumber"), it.seasonNumber},
            {QStringLiteral("episodeNumber"), it.episodeNumber},
            {QStringLiteral("episodeTitle"), it.episodeTitle},
            {QStringLiteral("status"), downloadStatusName(it.status)},
            {QStringLiteral("downloadedBytes"), it.downloadedBytes},
            {QStringLiteral("totalBytes"), it.totalBytes},
            {QStringLiteral("progressFraction"), it.progressFraction()},
            {QStringLiteral("errorMessage"), it.errorMessage},
            {QStringLiteral("localFileUri"), it.localFileUri},
        });
    return out;
}

QList<DownloadItem> DownloadManager::items() const { return m_items; }

void DownloadManager::load()
{
    m_items.clear();
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("downloads"));
    const auto raw = store.getString(profileKey(m_profileId).toStdString());
    if (!raw || raw->empty()) return;
    const QJsonArray arr =
        QJsonDocument::fromJson(QByteArray::fromStdString(*raw)).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        DownloadItem it;
        it.id = o.value(QStringLiteral("id")).toString();
        if (it.id.isEmpty()) continue;
        it.contentType = o.value(QStringLiteral("contentType")).toString();
        it.parentMetaId = o.value(QStringLiteral("parentMetaId")).toString();
        it.parentMetaType =
            o.value(QStringLiteral("parentMetaType")).toString();
        it.videoId = o.value(QStringLiteral("videoId")).toString();
        it.title = o.value(QStringLiteral("title")).toString();
        it.poster = o.value(QStringLiteral("poster")).toString();
        it.seasonNumber = o.value(QStringLiteral("seasonNumber")).toInt(-1);
        it.episodeNumber = o.value(QStringLiteral("episodeNumber")).toInt(-1);
        it.episodeTitle = o.value(QStringLiteral("episodeTitle")).toString();
        it.streamTitle = o.value(QStringLiteral("streamTitle")).toString();
        it.providerName = o.value(QStringLiteral("providerName")).toString();
        it.providerAddonId =
            o.value(QStringLiteral("providerAddonId")).toString();
        it.sourceUrl = o.value(QStringLiteral("sourceUrl")).toString();
        it.localFileUri = o.value(QStringLiteral("localFileUri")).toString();
        it.fileName = o.value(QStringLiteral("fileName")).toString();
        it.status =
            downloadStatusFromName(o.value(QStringLiteral("status")).toString());
        // Interrupted mid-download rows resume as Paused (never auto-run
        // network on launch; Compose restarts via explicit user action and
        // its own startup policy - here: user resumes explicitly).
        if (it.status == DownloadStatus::Downloading)
            it.status = DownloadStatus::Paused;
        it.downloadedBytes = static_cast<qint64>(
            o.value(QStringLiteral("downloadedBytes")).toDouble(0));
        it.totalBytes = static_cast<qint64>(
            o.value(QStringLiteral("totalBytes")).toDouble(-1));
        it.errorMessage = o.value(QStringLiteral("errorMessage")).toString();
        it.createdAtEpochMs = static_cast<qint64>(
            o.value(QStringLiteral("createdAtEpochMs")).toDouble(0));
        it.updatedAtEpochMs = static_cast<qint64>(
            o.value(QStringLiteral("updatedAtEpochMs")).toDouble(0));
        m_items.append(it);
    }
}

void DownloadManager::persist()
{
    QJsonArray arr;
    for (const DownloadItem& it : m_items) {
        arr.append(QJsonObject{
            {QStringLiteral("id"), it.id},
            {QStringLiteral("contentType"), it.contentType},
            {QStringLiteral("parentMetaId"), it.parentMetaId},
            {QStringLiteral("parentMetaType"), it.parentMetaType},
            {QStringLiteral("videoId"), it.videoId},
            {QStringLiteral("title"), it.title},
            {QStringLiteral("poster"), it.poster},
            {QStringLiteral("seasonNumber"), it.seasonNumber},
            {QStringLiteral("episodeNumber"), it.episodeNumber},
            {QStringLiteral("episodeTitle"), it.episodeTitle},
            {QStringLiteral("streamTitle"), it.streamTitle},
            {QStringLiteral("providerName"), it.providerName},
            {QStringLiteral("providerAddonId"), it.providerAddonId},
            {QStringLiteral("sourceUrl"), it.sourceUrl},
            {QStringLiteral("localFileUri"), it.localFileUri},
            {QStringLiteral("fileName"), it.fileName},
            {QStringLiteral("status"), downloadStatusName(it.status)},
            {QStringLiteral("downloadedBytes"),
             static_cast<double>(it.downloadedBytes)},
            {QStringLiteral("totalBytes"), static_cast<double>(it.totalBytes)},
            {QStringLiteral("errorMessage"), it.errorMessage},
            {QStringLiteral("createdAtEpochMs"),
             static_cast<double>(it.createdAtEpochMs)},
            {QStringLiteral("updatedAtEpochMs"),
             static_cast<double>(it.updatedAtEpochMs)},
        });
    }
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath("downloads"));
    store.putString(profileKey(m_profileId).toStdString(),
                    QString::fromUtf8(
                        QJsonDocument(arr).toJson(QJsonDocument::Compact))
                        .toStdString());
}

void DownloadManager::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    // Abort in-flight work: partial .part files stay for resume later.
    for (ActiveTransfer* t : m_active) {
        if (t->reply) t->reply->abort();
        if (t->file) {
            t->file->close();
            delete t->file;
        }
        delete t;
    }
    m_active.clear();
    m_profileId = profileId;
    load();
    emit changed();
}

void DownloadManager::mutate(
    const QString& id, const std::function<void(DownloadItem&)>& fn)
{
    for (DownloadItem& it : m_items) {
        if (it.id != id) continue;
        fn(it);
        it.updatedAtEpochMs = QDateTime::currentMSecsSinceEpoch();
        persist();
        emit changed();
        return;
    }
}

QString DownloadManager::nextDownloadId(qint64 nowEpochMs)
{
    // Process-wide ordinal (Compose nextDownloadOrdinal parity): ids must
    // be unique across manager instances sharing one profile store, even
    // within the same millisecond.
    static std::atomic<qint64> s_ordinal{0};
    return QString::number(nowEpochMs, 36) + u'_' +
           QString::number(++s_ordinal, 36);
}

QString DownloadManager::enqueue(
    const QString& contentType, const QString& parentMetaId,
    const QString& parentMetaType, const QString& videoId, const QString& title,
    const QString& poster, int seasonNumber, int episodeNumber,
    const QString& episodeTitle, const QString& streamTitle,
    const QString& providerName, const QString& sourceUrl)
{
    const QString url = sourceUrl.trimmed();
    if (url.isEmpty()) return QStringLiteral("MissingUrl");
    if (!isSupportedDownloadUrl(url))
        return QStringLiteral("UnsupportedFormat");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = buildLogicalKey(parentMetaId, seasonNumber,
                                        episodeNumber);
    bool replaced = false;
    for (const DownloadItem& it : m_items) {
        if (it.logicalContentKey() != key) continue;
        replaced = true;
        auto* doomed = m_active.take(it.id);
        if (doomed) {
            if (doomed->reply) doomed->reply->abort();
            if (doomed->file) {
                doomed->file->close();
                delete doomed->file;
            }
            delete doomed;
        }
        if (!it.localFileUri.isEmpty())
            QFile::remove(QUrl(it.localFileUri).toLocalFile());
        QFile::remove(downloadsDir() + u'/' + it.fileName +
                      QStringLiteral(".part"));
        m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                     [&](const DownloadItem& e) {
                                         return e.id == it.id;
                                     }),
                       m_items.end());
        break;   // logical keys are unique by construction
    }
    DownloadItem item;
    item.id = nextDownloadId(now);
    item.contentType = contentType;
    item.parentMetaId = parentMetaId.trimmed();
    item.parentMetaType = parentMetaType;
    item.videoId = videoId;
    item.title = title;
    item.poster = poster;
    item.seasonNumber = seasonNumber;
    item.episodeNumber = episodeNumber;
    item.episodeTitle = episodeTitle;
    item.streamTitle = streamTitle;
    item.providerName = providerName;
    item.sourceUrl = url;
    item.fileName =
        buildFileName(title, seasonNumber, episodeNumber, episodeTitle,
                      streamTitle, url, now);
    item.status = DownloadStatus::Downloading;
    item.createdAtEpochMs = now;
    item.updatedAtEpochMs = now;
    m_items.prepend(item);
    persist();
    emit changed();
    startTransfer(item, 1);
    return replaced ? QStringLiteral("Replaced")
                    : QStringLiteral("Started");
}

void DownloadManager::pauseDownload(const QString& downloadId)
{
    bool downloading = false;
    for (const DownloadItem& it : m_items) {
        if (it.id == downloadId && it.status == DownloadStatus::Downloading)
            downloading = true;
    }
    auto* handle = m_active.take(downloadId);
    if (handle) {
        if (handle->reply) handle->reply->abort();
        if (handle->file) {
            handle->file->close();
            delete handle->file;
        }
        delete handle;
    }
    if (!downloading) return;
    mutate(downloadId, [](DownloadItem& it) {
        if (it.status != DownloadStatus::Downloading) return;
        it.status = DownloadStatus::Paused;
        it.errorMessage.clear();
    });
}

void DownloadManager::resumeDownload(const QString& downloadId)
{
    for (const DownloadItem& it : m_items) {
        if (it.id != downloadId) continue;
        if (it.status != DownloadStatus::Paused &&
            it.status != DownloadStatus::Failed)
            return;
        DownloadItem fresh = it;
        fresh.status = DownloadStatus::Downloading;
        fresh.errorMessage.clear();
        fresh.localFileUri.clear();
        // Replace + persist without touching createdAt.
        for (DownloadItem& e : m_items) {
            if (e.id == downloadId) e = fresh;
        }
        persist();
        emit changed();
        startTransfer(fresh, 1);
        return;
    }
}

void DownloadManager::cancelDownload(const QString& downloadId)
{
    auto* handle = m_active.take(downloadId);
    QString fileUri;
    QString fileName;
    for (const DownloadItem& it : m_items) {
        if (it.id != downloadId) continue;
        fileUri = it.localFileUri;
        fileName = it.fileName;
    }
    if (handle) {
        if (handle->reply) handle->reply->abort();
        if (handle->file) {
            handle->file->close();
            delete handle->file;
        }
        delete handle;
    }
    if (!fileUri.isEmpty()) QFile::remove(QUrl(fileUri).toLocalFile());
    if (!fileName.isEmpty())
        QFile::remove(downloadsDir() + u'/' + fileName +
                      QStringLiteral(".part"));
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                 [&](const DownloadItem& it) {
                                     return it.id == downloadId;
                                 }),
                  m_items.end());
    persist();
    emit changed();
}

QString DownloadManager::playableLocalFile(const QString& parentMetaId,
                                           int seasonNumber, int episodeNumber,
                                           const QString& videoId) const
{
    // resolveLocalFileUri parity: the stored uri wins when its file still
    // exists, otherwise <downloadsDir>/<fileName> is the fallback (the
    // store keeps both spellings across renames/copies).
    const auto resolve = [this](const DownloadItem& it) -> QString {
        if (!it.localFileUri.isEmpty()) {
            const QString direct =
                QUrl(it.localFileUri).toLocalFile();
            if (!direct.isEmpty() && QFile::exists(direct))
                return it.localFileUri;
        }
        if (!it.fileName.isEmpty()) {
            const QString byName = downloadsDir() + u'/' + it.fileName;
            if (QFile::exists(byName))
                return QUrl::fromLocalFile(byName).toString();
        }
        return {};
    };
    const QString vid = videoId.trimmed();
    if (!vid.isEmpty()) {
        for (const DownloadItem& it : m_items) {
            if (it.videoId == vid && it.isPlayable()) {
                if (const QString uri = resolve(it); !uri.isEmpty())
                    return uri;
            }
        }
    }
    if (seasonNumber >= 0 && episodeNumber >= 0) {
        for (const DownloadItem& it : m_items) {
            if (it.parentMetaId.trimmed() == parentMetaId.trimmed() &&
                it.seasonNumber == seasonNumber &&
                it.episodeNumber == episodeNumber && it.isPlayable()) {
                if (const QString uri = resolve(it); !uri.isEmpty())
                    return uri;
            }
        }
        return {};
    }
    for (const DownloadItem& it : m_items) {
        if (it.parentMetaId.trimmed() == parentMetaId.trimmed() &&
            !it.isEpisode() && it.isPlayable()) {
            if (const QString uri = resolve(it); !uri.isEmpty())
                return uri;
        }
    }
    return {};
}

void DownloadManager::pauseActiveDownloads()
{
    QStringList ids;
    for (const DownloadItem& it : m_items) {
        if (it.status == DownloadStatus::Downloading) ids.append(it.id);
    }
    for (const QString& id : ids) pauseDownload(id);
}

bool DownloadManager::openDownloadsDirectory() const
{
    // Linux-target scope: xdg-open (the fork's desktop fallback path;
    // the AWT Desktop.open branch has no QtCore equivalent).
    return QProcess::startDetached(QStringLiteral("xdg-open"),
                                   {downloadsDir()});
}

void DownloadManager::startTransfer(DownloadItem item, int attempt)
{
    const QString partPath =
        downloadsDir() + u'/' + item.fileName + QStringLiteral(".part");
    qint64 resumeFrom = 0;
    QFile* probe = new QFile(partPath);
    if (probe->exists()) resumeFrom = std::max<qint64>(0, probe->size());
    delete probe;

    QNetworkRequest req{QUrl(item.sourceUrl)};
    req.setRawHeader("Accept", "*/*");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    if (resumeFrom > 0)
        req.setRawHeader("Range",
                         "bytes=" + QByteArray::number(resumeFrom) + "-");
    QNetworkReply* rep = m_nam->get(req);
    auto* transfer = new ActiveTransfer{};
    transfer->reply = rep;
    transfer->startedBytes = resumeFrom;
    transfer->attempt = attempt;
    transfer->id = item.id;
    // Replaces any stale handle (pause/resume races cancel first anyway).
    if (auto* old = m_active.take(item.id)) {
        if (old->reply) old->reply->abort();
        if (old->file) {
            old->file->close();
            delete old->file;
        }
        delete old;
    }
    m_active.insert(item.id, transfer);

    QFile* file = new QFile(partPath);
    const bool append = resumeFrom > 0;
    if (!file->open(append ? QIODevice::Append : QIODevice::WriteOnly)) {
        m_active.remove(item.id);
        delete transfer;
        delete file;
        mutate(item.id, [](DownloadItem& it) {
            if (it.status == DownloadStatus::Downloading)
                it.status = DownloadStatus::Failed;
        });
        return;
    }
    transfer->file = file;

    connect(rep, &QNetworkReply::downloadProgress, this,
            [this, id = item.id](qint64 received, qint64 total) {
                Q_UNUSED(total);
                auto* t = m_active.value(id, nullptr);
                if (!t) return;
                mutate(id, [&](DownloadItem& it) {
                    if (it.status != DownloadStatus::Downloading) return;
                    it.downloadedBytes = t->startedBytes + received;
                    const QVariant totalHeader =
                        t->reply ? t->reply->header(
                                       QNetworkRequest::
                                           ContentLengthHeader)
                                 : QVariant();
                    if (totalHeader.isValid()) {
                        const qint64 total = totalHeader.toLongLong();
                        if (total > 0)
                            it.totalBytes = t->startedBytes + total;
                    }
                    it.errorMessage.clear();
                });
            });
    connect(rep, &QNetworkReply::finished, this,
            [this, id = item.id, attempt] {
                auto* t = m_active.take(id);
                if (!t) return;
                QNetworkReply* r = t->reply;
                QFile* f = t->file;
                const qint64 startedBytes = t->startedBytes;
                delete t;
                const int status =
                    r ? r->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                            .toInt()
                      : 0;
                const bool netOk =
                    r && r->error() == QNetworkReply::NoError;
                // 416 (range unsatisfiable): restart from zero once.
                if (netOk && status == 416 && startedBytes > 0) {
                    if (f) {
                        f->close();
                        delete f;
                    }
                    if (r) r->deleteLater();
                    // Re-resolve the filename for a clean restart.
                    for (const DownloadItem& it : m_items) {
                        if (it.id != id) continue;
                        QFile::remove(downloadsDir() + u'/' + it.fileName +
                                      QStringLiteral(".part"));
                        DownloadItem fresh = it;
                        fresh.downloadedBytes = 0;
                        for (DownloadItem& e : m_items) {
                            if (e.id == id) e = fresh;
                        }
                        persist();
                        emit changed();
                        startTransfer(fresh, attempt);
                        return;
                    }
                    return;
                }
                if (f) {
                    f->close();
                    delete f;
                }
                if (r) r->deleteLater();
                if (!netOk || (status != 0 && (status < 200 || status >= 300))) {
                    // Retry within budget, else fail honestly.
                    bool retried = false;
                    for (const DownloadItem& it : m_items) {
                        if (it.id != id) continue;
                        if (it.status == DownloadStatus::Downloading &&
                            attempt < kMaxAttempts) {
                            DownloadItem fresh = it;
                            startTransfer(fresh, attempt + 1);
                            retried = true;
                        }
                    }
                    if (!retried) {
                        mutate(id, [](DownloadItem& it) {
                            if (it.status == DownloadStatus::Downloading) {
                                it.status = DownloadStatus::Failed;
                                it.errorMessage = QStringLiteral("Failed");
                            }
                        });
                    }
                    return;
                }
                // Success: total from headers when the row lacks one.
                qint64 total = -1;
                for (const DownloadItem& it : m_items) {
                    if (it.id != id) continue;
                    total = it.totalBytes > 0
                                ? it.totalBytes
                                : it.downloadedBytes;
                }
                QString fileName;
                for (const DownloadItem& it : m_items) {
                    if (it.id == id) fileName = it.fileName;
                }
                const QString finalPath = downloadsDir() + u'/' + fileName;
                QFile::remove(finalPath);
                QFile::rename(downloadsDir() + u'/' + fileName +
                                  QStringLiteral(".part"),
                              finalPath);
                mutate(id, [&](DownloadItem& it) {
                    if (it.status != DownloadStatus::Downloading) return;
                    it.status = DownloadStatus::Completed;
                    it.localFileUri =
                        QUrl::fromLocalFile(finalPath).toString();
                    if (total > 0) {
                        it.downloadedBytes = total;
                        it.totalBytes = total;
                    }
                    it.errorMessage.clear();
                });
            });
}

} // namespace nuvio::downloads
