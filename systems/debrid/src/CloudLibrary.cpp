#include "nuvio/debrid/CloudLibrary.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>

#include "nuvio/debrid/DebridSettings.h"

namespace nuvio::debrid {

namespace {
const char* const kCloudVideoExtensions[] = {
    "3g2", "3gp", "avi", "divx", "flv", "m2ts", "m4v", "mkv",  "mov",  "mp4",
    "mpeg", "mpg", "mts", "ogm", "ogv", "ts",   "webm", "wmv",
};

[[nodiscard]] QString normStr(const QJsonObject& o, const char* key)
{
    return o.value(QLatin1String(key)).toString().trimmed();
}

[[nodiscard]] QString scalarString(const QJsonValue& v)
{
    if (v.isDouble()) {
        const double n = v.toDouble();
        if (std::floor(n) == n) return QString::number(qint64(n));
        return QString::number(n);
    }
    return v.toString().trimmed();
}

[[nodiscard]] QString pathBasename(const QString& path)
{
    QString b = path;
    const int slash = std::max(b.lastIndexOf(u'/'), b.lastIndexOf(u'\\'));
    if (slash >= 0) b = b.mid(slash + 1);
    return b.trimmed();
}

[[nodiscard]] bool sameDisplayName(const QString& a, const QString& b)
{
    const auto norm = [](QString s) {
        s = s.trimmed();
        const int slash =
            std::max(s.lastIndexOf(u'/'), s.lastIndexOf(u'\\'));
        if (slash >= 0) s = s.mid(slash + 1);
        const int dot = s.lastIndexOf(u'.');
        if (dot > 0) s = s.left(dot);
        return s.trimmed().toLower();
    };
    const QString na = norm(a), nb = norm(b);
    return !na.isEmpty() && na == nb;
}

[[nodiscard]] bool usableCloudFileName(const QString& candidate,
                                       const QString& parentName,
                                       const QString& pathName)
{
    if (candidate.trimmed().isEmpty() ||
        sameDisplayName(candidate, parentName))
        return false;
    QString pathNoExt = pathName;
    const int dot = pathNoExt.lastIndexOf(u'.');
    if (dot > 0) pathNoExt = pathNoExt.left(dot);
    if (!candidate.contains(u'.') && sameDisplayName(candidate, pathNoExt))
        return false;
    return true;
}

[[nodiscard]] QString bestCloudFileName(const QString& name,
                                        const QString& shortName,
                                        const QString& absolutePath,
                                        const QString& parentName)
{
    const QString pathName = pathBasename(absolutePath);
    const bool rawIsPath =
        name.contains(u'/') || name.contains(u'\\');
    QString rawBase = name.trimmed();
    if (rawIsPath) {
        rawBase = pathBasename(rawBase);
        if (rawBase.trimmed().isEmpty()) rawBase.clear();
    }
    const QStringList candidates{
        shortName.trimmed(), rawBase,
        rawIsPath ? QString() : name.trimmed(), pathName, name.trimmed(),
        absolutePath.trimmed(),
    };
    for (const QString& c : candidates) {
        if (!c.isEmpty() && usableCloudFileName(c, parentName, pathName))
            return c;
    }
    for (const QString& c : candidates) {
        if (!c.isEmpty()) return c;
    }
    return {};
}

[[nodiscard]] double progressFraction(double v)
{
    const double n = v > 1.0 ? v / 100.0 : v;
    return std::clamp(n, 0.0, 1.0);
}
} // namespace

bool cloudFilePlayable(const QString& name, const QString& mimeType)
{
    if (mimeType.trimmed().toLower().startsWith(QLatin1String("video/")))
        return true;
    QString ext = name;
    const int dot = ext.lastIndexOf(u'.');
    if (dot < 0) return false;
    ext = ext.mid(dot + 1).toLower();
    for (const char* known : kCloudVideoExtensions) {
        if (ext == QLatin1String(known)) return true;
    }
    return false;
}

QList<CloudLibraryItem> parseTorboxCloudList(const QByteArray& body,
                                             const QString& type)
{
    QList<CloudLibraryItem> out;
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (!root.value(QStringLiteral("success")).toBool(false)) return out;
    for (const QJsonValue& iv :
         root.value(QStringLiteral("data")).toArray()) {
        const QJsonObject o = iv.toObject();
        QString itemId = scalarString(o.value(QStringLiteral("id")));
        if (itemId.isEmpty())
            itemId = normStr(o, "hash");
        if (itemId.isEmpty()) continue;
        QString itemName = normStr(o, "name");
        if (itemName.isEmpty()) itemName = itemId;
        QList<CloudLibraryFile> files;
        for (const QJsonValue& fv :
             o.value(QStringLiteral("files")).toArray()) {
            const QJsonObject fo = fv.toObject();
            const QString fileId = scalarString(fo.value(QStringLiteral("id")));
            QString mime = normStr(fo, "mimetype");
            // Some responses use mime_type instead (tolerant read).
            if (mime.isEmpty()) mime = normStr(fo, "mime_type");
            const QString fileName = bestCloudFileName(
                normStr(fo, "name"), normStr(fo, "short_name"),
                normStr(fo, "absolute_path"), itemName);
            if (fileName.isEmpty()) continue;
            CloudLibraryFile f;
            f.id = fileId;
            f.name = fileName;
            if (fo.value(QStringLiteral("size")).isDouble())
                f.sizeBytes = static_cast<qint64>(
                    fo.value(QStringLiteral("size")).toDouble());
            f.mimeType = mime;
            f.playable =
                !fileId.isEmpty() && cloudFilePlayable(fileName, mime);
            files.append(f);
        }
        CloudLibraryItem item;
        item.providerId = QStringLiteral("torbox");
        item.providerName = QStringLiteral("Torbox");
        item.id = itemId;
        item.type = type;
        item.name = itemName;
        const QStringList states{normStr(o, "status"), normStr(o, "download_state"),
                                 normStr(o, "state")};
        for (const QString& s : states) {
            if (!s.isEmpty()) {
                item.status = s;
                break;
            }
        }
        qint64 size = -1;
        if (o.value(QStringLiteral("size")).isDouble())
            size = static_cast<qint64>(
                o.value(QStringLiteral("size")).toDouble());
        else if (o.value(QStringLiteral("total_size")).isDouble())
            size = static_cast<qint64>(
                o.value(QStringLiteral("total_size")).toDouble());
        if (size < 0) {
            qint64 sum = 0;
            bool any = false;
            for (const CloudLibraryFile& f : files) {
                if (f.sizeBytes >= 0) {
                    sum += f.sizeBytes;
                    any = true;
                }
            }
            if (any) size = sum;
        }
        item.sizeBytes = size;
        double prog = -1.0;
        bool hasProg = false;
        if (o.value(QStringLiteral("progress")).isDouble()) {
            prog = o.value(QStringLiteral("progress")).toDouble();
            hasProg = true;
        } else if (o.value(QStringLiteral("download_progress")).isDouble()) {
            prog = o.value(QStringLiteral("download_progress")).toDouble();
            hasProg = true;
        }
        item.progressFraction = hasProg ? progressFraction(prog) : -1.0;
        item.files = files;
        out.append(item);
    }
    return out;
}

QList<CloudLibraryItem> parsePremiumizeCloudList(const QByteArray& body)
{
    QList<CloudLibraryItem> out;
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    struct Mapped {
        QString groupKey;
        QString itemId;
        QString itemName;
        CloudLibraryFile file;
    };
    QList<Mapped> mapped;
    for (const QJsonValue& fv :
         root.value(QStringLiteral("files")).toArray()) {
        const QJsonObject fo = fv.toObject();
        QString path = normStr(fo, "path");
        while (path.startsWith(u'/')) path = path.mid(1);
        if (path.isEmpty()) continue;
        QString fileName = normStr(fo, "name");
        if (fileName.isEmpty()) {
            fileName = pathBasename(path);
            if (fileName.isEmpty()) continue;
        }
        const QString fileId = normStr(fo, "id");
        const QString mime = normStr(fo, "mime_type");
        const bool playable = cloudFilePlayable(fileName, mime);
        const QStringList segments = path.split(u'/', Qt::SkipEmptyParts);
        const bool isRootFile = segments.size() <= 1;
        const QString topLevel = segments.value(0, fileName);
        const QString itemName = isRootFile ? fileName : topLevel;
        QString itemId;
        QString groupKey;
        if (isRootFile) {
            itemId = "file:" + (!fileId.isEmpty() ? fileId
                                : !path.isEmpty() ? path
                                                  : fileName);
            groupKey = itemId;
        } else {
            itemId = "folder:" + topLevel;
            groupKey = itemId;
        }
        CloudLibraryFile file;
        file.id = fileId;
        file.name = fileName;
        if (fo.value(QStringLiteral("size")).isDouble())
            file.sizeBytes = static_cast<qint64>(
                fo.value(QStringLiteral("size")).toDouble());
        file.mimeType = mime;
        file.playable = playable;
        file.playbackUrl = normStr(fo, "link");
        if (!playable) file.playbackUrl.clear();
        mapped.append({groupKey, itemId, itemName, file});
    }
    QHash<QString, int> groupIndex;
    for (const Mapped& m : mapped) {
        if (groupIndex.contains(m.groupKey)) continue;
        groupIndex.insert(m.groupKey, out.size());
        CloudLibraryItem item;
        item.providerId = QStringLiteral("premiumize");
        item.providerName = QStringLiteral("Premiumize");
        item.id = m.itemId;
        item.type = QStringLiteral("File");
        item.name = m.itemName;
        item.status = QStringLiteral("Ready");
        out.append(item);
    }
    for (const Mapped& m : mapped) {
        CloudLibraryItem& item = out[groupIndex.value(m.groupKey)];
        item.files.append(m.file);
    }
    // Playable first, then name (Compose sort parity).
    for (CloudLibraryItem& item : out) {
        std::sort(item.files.begin(), item.files.end(),
                  [](const CloudLibraryFile& a, const CloudLibraryFile& b) {
                      if (a.playable != b.playable) return a.playable;
                      return a.name.toLower() < b.name.toLower();
                  });
        qint64 sum = 0;
        bool any = false;
        for (const CloudLibraryFile& f : item.files) {
            if (f.sizeBytes >= 0) {
                sum += f.sizeBytes;
                any = true;
            }
        }
        if (any) item.sizeBytes = sum;
    }
    return out;
}

CloudLibrary::CloudLibrary(DebridSettings* settings, QObject* parent)
    : QObject(parent),
      m_settings(settings),
      m_nam(new QNetworkAccessManager(this))
{}

QVariantList CloudLibrary::itemsVariant() const
{
    QVariantList out;
    for (const CloudLibraryItem& item : m_items) {
        QVariantList files;
        for (const CloudLibraryFile& f : item.files)
            files.append(QVariantMap{
                {QStringLiteral("id"), f.id},
                {QStringLiteral("name"), f.name},
                {QStringLiteral("sizeBytes"), f.sizeBytes},
                {QStringLiteral("playable"), f.playable},
            });
        out.append(QVariantMap{
            {QStringLiteral("providerId"), item.providerId},
            {QStringLiteral("providerName"), item.providerName},
            {QStringLiteral("id"), item.id},
            {QStringLiteral("type"), item.type},
            {QStringLiteral("name"), item.name},
            {QStringLiteral("status"), item.status},
            {QStringLiteral("sizeBytes"), item.sizeBytes},
            {QStringLiteral("progressFraction"), item.progressFraction},
            {QStringLiteral("files"), files},
        });
    }
    return out;
}

void CloudLibrary::refresh()
{
    if (!m_settings || !m_settings->enabled() ||
        !m_settings->cloudLibraryEnabled()) {
        m_items.clear();
        m_error.clear();
        emit changed();
        return;
    }
    const quint64 token = ++m_token;
    m_items.clear();
    m_error.clear();
    m_pending = 0;
    emit changed();
    struct Job {
        QString providerId;
        QStringList urls;
    };
    QList<Job> jobs;
    const QString torboxKey =
        m_settings->providerApiKey(QStringLiteral("torbox")).trimmed();
    if (!torboxKey.isEmpty()) {
        jobs.append({QStringLiteral("torbox"),
                     {QStringLiteral("https://api.torbox.app/v1/api/torrents/"
                                     "mylist"),
                      QStringLiteral("https://api.torbox.app/v1/api/usenet/"
                                     "mylist"),
                      QStringLiteral("https://api.torbox.app/v1/api/webdl/"
                                     "mylist")}});
    }
    const QString pmKey =
        m_settings->providerApiKey(QStringLiteral("premiumize")).trimmed();
    if (!pmKey.isEmpty()) {
        jobs.append({QStringLiteral("premiumize"),
                     {QStringLiteral(
                         "https://www.premiumize.me/api/item/listall")}});
    }
    if (jobs.isEmpty()) return;
    for (const Job& job : jobs) {
        for (const QString& url : job.urls) {
            ++m_pending;
            QNetworkRequest req{QUrl(url)};
            req.setRawHeader("Accept", "application/json");
            const QString key = job.providerId == QLatin1String("torbox")
                                    ? torboxKey
                                    : pmKey;
            req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
            QNetworkReply* rep = m_nam->get(req);
            connect(rep, &QNetworkReply::finished, this,
                    [this, rep, job, url, token] {
                        rep->deleteLater();
                        if (token != m_token) return;
                        if (--m_pending < 0) m_pending = 0;
                        if (rep->error() != QNetworkReply::NoError) {
                            m_error = rep->errorString();
                            emit changed();
                            return;
                        }
                        const QByteArray body = rep->readAll();
                        QList<CloudLibraryItem> items;
                        if (job.providerId == QLatin1String("torbox")) {
                            QString type = QStringLiteral("Torrent");
                            if (url.contains(QLatin1String("/usenet/")))
                                type = QStringLiteral("Usenet");
                            else if (url.contains(QLatin1String("/webdl/")))
                                type = QStringLiteral("WebDownload");
                            items = parseTorboxCloudList(body, type);
                        } else {
                            items = parsePremiumizeCloudList(body);
                        }
                        m_items.append(items);
                        emit changed();
                    });
        }
    }
}

void CloudLibrary::resolvePlayback(const QString& providerId,
                                   const QString& itemId,
                                   const QString& itemType,
                                   const QString& fileId)
{
    const quint64 token = ++m_token;
    const CloudLibraryItem* item = nullptr;
    for (const CloudLibraryItem& it : m_items) {
        if (it.providerId == providerId && it.id == itemId) item = &it;
    }
    if (!item || !m_settings) {
        emit playbackFailed(QStringLiteral("Unknown cloud item"));
        return;
    }
    const CloudLibraryFile* file = nullptr;
    for (const CloudLibraryFile& f : item->files) {
        if (f.id == fileId) file = &f;
    }
    if (!file || !file->playable) {
        emit playbackFailed(QStringLiteral("File is not playable"));
        return;
    }
    // Premiumize list links ride along; Torbox-style items resolve per
    // type through the request-download endpoints (D2 shapes).
    if (providerId == QLatin1String("premiumize") &&
        !file->playbackUrl.isEmpty()) {
        emit playbackResolved(file->playbackUrl, file->name);
        return;
    }
    if (providerId == QLatin1String("premiumize")) {
        const QString key = m_settings->providerApiKey(providerId).trimmed();
        QUrl details(
            QStringLiteral("https://www.premiumize.me/api/item/details"));
        QUrlQuery dq;
        dq.addQueryItem(QStringLiteral("id"), file->id);
        details.setQuery(dq);
        QNetworkRequest req{details};
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        QNetworkReply* rep = m_nam->get(req);
        connect(rep, &QNetworkReply::finished, this,
                [this, rep, token, file = *file] {
                    rep->deleteLater();
                    if (token != m_token) return;
                    const QJsonObject o = QJsonDocument::fromJson(rep->readAll())
                                              .object();
                    if (o.value(QStringLiteral("status"))
                            .toString()
                            .compare(QLatin1String("error"),
                                     Qt::CaseInsensitive) == 0) {
                        emit playbackFailed(
                            o.value(QStringLiteral("message")).toString());
                        return;
                    }
                    const QString url =
                        o.value(QStringLiteral("link")).toString();
                    if (url.isEmpty()) {
                        emit playbackFailed(QStringLiteral("No link"));
                        return;
                    }
                    emit playbackResolved(url, file.name);
                });
        return;
    }
    const QString key =
        m_settings->providerApiKey(providerId).trimmed();
    if (key.isEmpty() || !m_settings) {
        emit playbackFailed(QStringLiteral("Not connected"));
        return;
    }
    // Torbox per-type request-download endpoints (D2 URL shapes).
    QString endpoint;
    QString idParam;
    if (itemType == QLatin1String("Torrent")) {
        endpoint = QStringLiteral(
            "https://api.torbox.app/v1/api/torrents/requestdl");
        idParam = QStringLiteral("torrent_id");
    } else if (itemType == QLatin1String("Usenet")) {
        endpoint =
            QStringLiteral("https://api.torbox.app/v1/api/usenet/requestdl");
        idParam = QStringLiteral("usenet_id");
    } else if (itemType == QLatin1String("WebDownload")) {
        endpoint =
            QStringLiteral("https://api.torbox.app/v1/api/webdl/requestdl");
        idParam = QStringLiteral("web_id");
    } else {
        emit playbackFailed(QStringLiteral("Unsupported cloud type"));
        return;
    }
    QUrl url(endpoint);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("token"), key);
    q.addQueryItem(idParam, item->id);
    q.addQueryItem(QStringLiteral("file_id"), file->id);
    q.addQueryItem(QStringLiteral("zip_link"), QStringLiteral("false"));
    q.addQueryItem(QStringLiteral("redirect"), QStringLiteral("false"));
    q.addQueryItem(QStringLiteral("append_name"), QStringLiteral("false"));
    url.setQuery(q);
    QNetworkRequest req{url};
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    QNetworkReply* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, token, file = *file] {
                rep->deleteLater();
                if (token != m_token) return;
                const QJsonObject root =
                    QJsonDocument::fromJson(rep->readAll()).object();
                if (!root.value(QStringLiteral("success")).toBool(false)) {
                    emit playbackFailed(
                        root.value(QStringLiteral("detail")).toString(
                            root.value(QStringLiteral("error")).toString()));
                    return;
                }
                const QString dl =
                    root.value(QStringLiteral("data")).toString();
                if (dl.isEmpty()) {
                    emit playbackFailed(QStringLiteral("No link"));
                    return;
                }
                emit playbackResolved(dl, file.name);
            });
}

} // namespace nuvio::debrid
