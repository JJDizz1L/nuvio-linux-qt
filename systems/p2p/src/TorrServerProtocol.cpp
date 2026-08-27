#include "nuvio/p2p/TorrServerProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace nuvio::p2p {

namespace {

const char* const kDefaultTrackers[] = {
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://tracker.openbittorrent.com:6969/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://tracker.torrent.eu.org:451/announce",
};

/// Case-insensitive video extensions; MUST stay byte-parity with the
/// Compose line's VIDEO_EXTENSIONS set.
const char* const kVideoExtensions[] = {
    "mkv", "mp4", "avi", "webm", "ts", "m4v", "mov", "wmv", "flv",
};

QString percentEncodeQueryValue(const QString& value)
{
    // Qt keeps exactly the RFC3986 unreserved set (A-Z a-z 0-9 - . _ ~)
    // verbatim and emits UPPERCASE %XX for the rest - byte-identical to
    // the Compose line's hand-rolled encodeP2pQueryValue().
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

} // namespace

// ---- request bodies ---------------------------------------------------------

QByteArray addTorrentRequestBody(const QString& magnetLink)
{
    const QJsonObject body{{QLatin1String("action"), QStringLiteral("add")},
                           {QLatin1String("link"),   magnetLink},
                           {QLatin1String("save_to_db"), false}};
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QByteArray getTorrentRequestBody(const QString& hash)
{
    const QJsonObject body{{QLatin1String("action"), QStringLiteral("get")},
                           {QLatin1String("hash"),   hash}};
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QByteArray dropTorrentRequestBody(const QString& hash)
{
    const QJsonObject body{{QLatin1String("action"), QStringLiteral("drop")},
                           {QLatin1String("hash"),   hash}};
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

// ---- parsing -----------------------------------------------------------------

std::optional<TorrentStats> parseTorrentStats(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject o = doc.object();

    TorrentStats s;
    s.downloadSpeedBps =
        o.value(QLatin1String("download_speed")).toVariant().value<qint64>();
    s.uploadSpeedBps =
        o.value(QLatin1String("upload_speed")).toVariant().value<qint64>();
    s.peers = o.value(QLatin1String("active_peers")).toInt();
    s.seeds = o.value(QLatin1String("connected_seeders")).toInt();
    s.preloadedBytes =
        o.value(QLatin1String("preloaded_bytes")).toVariant().value<qint64>();
    s.loadedSize =
        o.value(QLatin1String("loaded_size")).toVariant().value<qint64>();
    s.torrentSize =
        o.value(QLatin1String("torrent_size")).toVariant().value<qint64>();

    const QJsonArray filesArr = o.value(QLatin1String("file_stats")).toArray();
    int index = 0;
    for (const auto& v : filesArr) {
        const QJsonObject f = v.toObject();
        TorrentFile tf;
        tf.id          = f.contains(QLatin1String("id"))
                             ? f.value(QLatin1String("id")).toInt(index + 1)
                             : index + 1;
        tf.path = f.value(QLatin1String("path")).toString();
        tf.lengthBytes =
            f.value(QLatin1String("length")).toVariant().value<qint64>();
        s.files.append(tf);
        ++index;
    }
    return s;
}

// ---- policy -------------------------------------------------------------------

QString buildMagnetUri(const QString& infoHash,
                       const QStringList& extraTrackers)
{
    // Canonicalization parity (canonicalP2pInfoHash): trim + lowercase,
    // exactly 40 or 64 hex chars, else reject.
    const QString canonical = infoHash.trimmed().toLower();
    bool hexOnly = canonical.size() == 40 || canonical.size() == 64;
    for (const QChar ch : canonical) {
        const bool isHex =
            (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f'));
        if (!isHex) { hexOnly = false; break; }
    }
    if (!hexOnly) return {};

    const QString topic = canonical.size() == 40
        ? QStringLiteral("urn:btih:") + canonical
        : QStringLiteral("urn:btmh:1220") + canonical;

    QStringList trackers;
    for (const char* t : kDefaultTrackers)
        trackers.append(QString::fromLatin1(t));
    trackers.append(extraTrackers);

    QString uri  = QStringLiteral("magnet:?xt=") + topic;
    QStringList seen;
    for (const QString& t : trackers) {
        if (t.trimmed().isEmpty()) continue;   // blank filtered
        if (seen.contains(t))      continue;   // distinct, order kept
        seen.append(t);
        uri += QStringLiteral("&tr=") + percentEncodeQueryValue(t);
    }
    return uri;
}

QString buildStreamUrl(const QString& baseUrl, const QString& magnetLink,
                       int fileIndex)
{
    QString base = baseUrl;
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    return base + QStringLiteral("/stream?link=")
         + percentEncodeQueryValue(magnetLink)
         + QStringLiteral("&index=") + QString::number(fileIndex)
         + QStringLiteral("&play");
}

int resolveFileIndex(const QList<TorrentFile>& files, int requestedIdx,
                     const QString& filename)
{
    if (files.isEmpty()) return 1;

    const QString wanted = filename.trimmed();
    if (!wanted.isEmpty()) {
        for (const auto& f : files) {                       // 1. exact name
            const QString base = f.path.section(QLatin1Char('/'), -1);
            if (base.compare(wanted, Qt::CaseInsensitive) == 0) return f.id;
        }
        for (const auto& f : files)                         // 2. contains
            if (f.path.contains(wanted, Qt::CaseInsensitive)) return f.id;
    }

    if (requestedIdx >= 0) {
        const int stremioOffsetId = requestedIdx + 1;       // 3. id offset
        for (const auto& f : files)
            if (f.id == stremioOffsetId) return f.id;

        if (requestedIdx < files.size())                    // 4. positional
            return files.at(requestedIdx).id;
    }

    const TorrentFile* biggestVideo = nullptr;              // 5. largest video
    const TorrentFile* biggestAny   = nullptr;              // 6. largest any
    for (const auto& f : files) {
        if (!biggestAny || f.lengthBytes > biggestAny->lengthBytes)
            biggestAny = &f;
        const QString ext = f.path.section(QLatin1Char('.'), -1).toLower();
        for (const char* ve : kVideoExtensions) {
            if (ext == QLatin1String(ve)) {
                if (!biggestVideo
                    || f.lengthBytes > biggestVideo->lengthBytes)
                    biggestVideo = &f;
                break;
            }
        }
    }
    if (biggestVideo) return biggestVideo->id;
    if (biggestAny)   return biggestAny->id;
    return 1;                                               // 7. last resort
}

bool mergeCacheSettings(const QByteArray& currentSettingsJson, int cacheMb,
                        QByteArray* updatedOut)
{
    const QJsonDocument doc = QJsonDocument::fromJson(currentSettingsJson);
    if (!doc.isObject()) return false;
    QJsonObject updated = doc.object();
    updated.insert(QLatin1String("cache"), cacheMb);
    if (updatedOut)
        *updatedOut = QJsonDocument(updated).toJson(QJsonDocument::Compact);
    return true;
}

} // namespace nuvio::p2p