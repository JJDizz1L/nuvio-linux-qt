#include "nuvio/debrid/DebridResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>

#include "nuvio/debrid/DebridApi.h"
#include "nuvio/debrid/DebridFileSelect.h"
#include "nuvio/debrid/DebridSettings.h"
#include "nuvio/debrid/DebridTypes.h"

namespace nuvio::debrid {

QString activeResolverProviderId(const DebridSettings& settings)
{
    if (!settings.enabled()) return {};
    const QString preferred = settings.preferredResolverProviderId().trimmed();
    QStringList configured;
    for (const DebridProvider& p : allProviders()) {
        if (!p.visibleInUi) continue;
        if (settings.providerApiKey(p.id).trimmed().isEmpty()) continue;
        if (p.id == QLatin1String("torbox") ||
            p.id == QLatin1String("premiumize"))
            configured.append(p.id);
    }
    if (configured.isEmpty()) return {};
    if (configured.contains(preferred)) return preferred;
    return configured.first();
}

QString magnetForHash(const QString& infoHashHex, const QString& title)
{
    QString magnet = QStringLiteral("magnet:?xt=urn:btih:") +
                     infoHashHex.trimmed().toLower();
    if (!title.trimmed().isEmpty())
        magnet += QStringLiteral("&dn=") +
                  QString::fromUtf8(
                      QUrl::toPercentEncoding(title.trimmed()));
    return magnet;
}

PremiumizeFailure classifyPremiumizeError(int httpStatus,
                                          const QString& messageLower)
{
    if (httpStatus == 401 || httpStatus == 403)
        return PremiumizeFailure::Error;
    if (messageLower.contains(QLatin1String("cache")) ||
        messageLower.contains(QLatin1String("not found")))
        return PremiumizeFailure::NotCached;
    return PremiumizeFailure::Stale;
}

namespace {
// Verbatim multipart shape (Compose multipartFormBody): boundary lines +
// closing delimiter, CRLF throughout. The create endpoint requires it;
// JSON is NOT accepted there.
QByteArray multipartBody(const QString& boundary,
                         const QList<QPair<QString, QString>>& fields)
{
    QByteArray out;
    for (const auto& [name, value] : fields) {
        out += "--" + boundary.toUtf8() + "\r\n";
        out += "Content-Disposition: form-data; name=\"" + name.toUtf8() +
               "\"\r\n\r\n";
        out += value.toUtf8() + "\r\n";
    }
    out += "--" + boundary.toUtf8() + "--\r\n";
    return out;
}
} // namespace

DebridResolver::DebridResolver(DebridSettings* settings, QObject* parent)
    : QObject(parent),
      m_settings(settings),
      m_nam(new QNetworkAccessManager(this))
{}

void DebridResolver::setEndpointOverride(const QString& providerId,
                                         const QString& baseUrl)
{
    if (baseUrl.trimmed().isEmpty()) m_baseOverrides.remove(providerId);
    else m_baseOverrides.insert(providerId, baseUrl.trimmed());
}

QString DebridResolver::torboxUrl(const QString& path) const
{
    return m_baseOverrides.value(QStringLiteral("torbox"),
                                 QStringLiteral("https://api.torbox.app")) +
           path;
}

QString DebridResolver::premiumizeUrl(const QString& path) const
{
    return m_baseOverrides.value(QStringLiteral("premiumize"),
                                 QStringLiteral("https://www.premiumize.me")) +
           path;
}

bool DebridResolver::canResolve() const
{
    return m_settings && m_settings->enabled() &&
           !activeResolverProviderId(*m_settings).isEmpty();
}

void DebridResolver::resolveTorrent(const QString& infoHashHex,
                                    const QString& title, int season,
                                    int episode)
{
    const quint64 token = ++m_token;
    const QString hash = infoHashHex.trimmed().toLower();
    m_pendingKey = hash;
    if (!m_settings || !m_settings->enabled() || hash.isEmpty()) {
        finishFail(QStringLiteral("NoCredential"), token);
        return;
    }
    const QString providerId = activeResolverProviderId(*m_settings);
    const QString apiKey =
        m_settings->providerApiKey(providerId).trimmed();
    if (providerId.isEmpty() || apiKey.isEmpty()) {
        finishFail(QStringLiteral("NoCredential"), token);
        return;
    }
    const QString magnet = magnetForHash(hash, title);
    if (providerId == QLatin1String("torbox")) {
        const QUrl checkUrl(torboxUrl(QStringLiteral(
            "/v1/api/torrents/checkcached?format=object")));
        QNetworkRequest checkReq{checkUrl};
        checkReq.setHeader(QNetworkRequest::ContentTypeHeader,
                           "application/json");
        checkReq.setRawHeader("Accept", "application/json");
        checkReq.setRawHeader("Authorization",
                              "Bearer " + apiKey.toUtf8());
        QNetworkReply* rep = m_nam->post(
            checkReq, torbox::checkCachedBody({hash}));
        connect(rep, &QNetworkReply::finished, this,
                [this, rep, apiKey, magnet, title, season, episode,
                 hash, token] {
                    rep->deleteLater();
                    if (token != m_token || m_pendingKey != hash) return;
                    // Presence in the response map means cached (Compose
                    // isCached parity); absence is inconclusive, so
                    // resolution proceeds and creation decides.
                    resolveTorbox(apiKey, magnet, title, season, episode,
                                  token);
                });
        return;
    }
    if (providerId == QLatin1String("premiumize")) {
        resolvePremiumize(apiKey, magnet, title, season, episode, token);
        return;
    }
    finishFail(QStringLiteral("Error"), token);   // realdebrid: never auto
}

void DebridResolver::resolveTorbox(const QString& apiKey,
                                   const QString& magnet, const QString& title,
                                   int season, int episode, quint64 token)
{
    Q_UNUSED(title);
    const QString hash = m_pendingKey;
    const QString boundary =
        QStringLiteral("NuvioDebrid") +
        QString::number(qHash(magnet), 16);
    QNetworkRequest req{QUrl(torboxUrl(
        QStringLiteral("/v1/api/torrents/createtorrent")))};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + boundary.toUtf8());
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    QNetworkReply* rep = m_nam->post(
        req, multipartBody(boundary,
                           {{"magnet", magnet},
                            {"add_only_if_cached", "true"},
                            {"allow_zip", "false"}}));
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, apiKey, title, season, episode, hash, token] {
                rep->deleteLater();
                if (token != m_token || m_pendingKey != hash) return;
                const QJsonObject root =
                    QJsonDocument::fromJson(rep->readAll()).object();
                const QJsonObject data =
                    root.value(QStringLiteral("data")).toObject();
                const bool ok =
                    root.value(QStringLiteral("success")).toBool(false);
                int torrentId = data.value(QStringLiteral("torrent_id")).toInt(-1);
                if (torrentId < 0)
                    torrentId = data.value(QStringLiteral("id")).toInt(-1);
                if (!ok || torrentId < 0) {
                    // Creation refused (not cached / bad key): surface
                    // NotCached when the envelope says so, else Stale.
                    const QString err =
                        root.value(QStringLiteral("error")).toString() +
                        root.value(QStringLiteral("detail")).toString();
                    const QString lower = err.toLower();
                    if (lower.contains(QLatin1String("cached")) ||
                        lower.contains(QLatin1String("not found")))
                        finishFail(QStringLiteral("NotCached"), token);
                    else
                        finishFail(QStringLiteral("Stale"), token);
                    return;
                }
                QNetworkRequest getReq{QUrl(
                    torboxUrl(QStringLiteral("/v1/api/torrents/mylist?"
                                              "bypass_cache=true&id=") +
                              QString::number(torrentId)))};
                getReq.setRawHeader("Accept", "application/json");
                getReq.setRawHeader("Authorization",
                                    "Bearer " + apiKey.toUtf8());
                QNetworkReply* getRep = m_nam->get(getReq);
                connect(getRep, &QNetworkReply::finished, this,
                        [this, getRep, apiKey, title, season, episode, hash,
                         token, torrentId] {
                            getRep->deleteLater();
                            if (token != m_token || m_pendingKey != hash)
                                return;
                            const QJsonObject detail =
                                QJsonDocument::fromJson(getRep->readAll())
                                    .object();
                            const QJsonObject tdata =
                                detail.value(QStringLiteral("data")).toObject();
                            QList<TorrentFile> files;
                            for (const QJsonValue& fv :
                                 tdata.value(QStringLiteral("files")).toArray()) {
                                const QJsonObject fo = fv.toObject();
                                TorrentFile f;
                                f.id = fo.value(QStringLiteral("id")).toInt(-1);
                                const QString name = fo.value(QStringLiteral("name")).toString();
                                const QString shortName =
                                    fo.value(QStringLiteral("short_name")).toString();
                                const QString absPath =
                                    fo.value(QStringLiteral("absolute_path")).toString();
                                f.name = !name.isEmpty()   ? name
                                         : !shortName.isEmpty() ? shortName
                                                                : absPath;
                                f.mimeType =
                                    fo.value(QStringLiteral("mimetype")).toString();
                                f.size = static_cast<qint64>(
                                    fo.value(QStringLiteral("size")).toDouble(0));
                                files.append(f);
                            }
                            const auto picked = selectTorrentFile(
                                files, {}, season, episode);
                            if (!picked || picked->id < 0) {
                                finishFail(QStringLiteral("Stale"), token);
                                return;
                            }
                            QUrl dl(torboxUrl(QStringLiteral(
                                "/v1/api/torrents/requestdl")));
                            QUrlQuery q;
                            q.addQueryItem(QStringLiteral("token"), apiKey);
                            q.addQueryItem(QStringLiteral("torrent_id"),
                                           QString::number(torrentId));
                            q.addQueryItem(QStringLiteral("file_id"),
                                           QString::number(picked->id));
                            q.addQueryItem(QStringLiteral("zip_link"),
                                           QStringLiteral("false"));
                            q.addQueryItem(QStringLiteral("redirect"),
                                           QStringLiteral("false"));
                            q.addQueryItem(QStringLiteral("append_name"),
                                           QStringLiteral("false"));
                            dl.setQuery(q);
                            QNetworkRequest dlReq{dl};
                            dlReq.setRawHeader("Accept", "application/json");
                            dlReq.setRawHeader("Authorization",
                                               "Bearer " + apiKey.toUtf8());
                            QNetworkReply* dlRep = m_nam->get(dlReq);
                            connect(dlRep, &QNetworkReply::finished, this,
                                    [this, dlRep, picked, hash, token] {
                                        dlRep->deleteLater();
                                        if (token != m_token ||
                                            m_pendingKey != hash)
                                            return;
                                        const QJsonObject dlRoot =
                                            QJsonDocument::fromJson(
                                                dlRep->readAll())
                                                .object();
                                        const QString url =
                                            dlRoot
                                                .value(QStringLiteral("data"))
                                                .toString();
                                        if (url.isEmpty()) {
                                            finishFail(QStringLiteral("Stale"),
                                                       token);
                                            return;
                                        }
                                        finishOk(url, picked->name,
                                                 QStringLiteral("torbox"),
                                                 token);
                                    });
                        });
            });
}

void DebridResolver::resolvePremiumize(const QString& apiKey,
                                       const QString& magnet,
                                       const QString& title, int season,
                                       int episode, quint64 token)
{
    Q_UNUSED(title);
    const QString hash = m_pendingKey;
    QNetworkRequest req{QUrl(premiumizeUrl(
        QStringLiteral("/api/transfer/directdl")))};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    QNetworkReply* rep = m_nam->post(
        req, realdebrid::formBody({{"src", magnet}}));
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, season, episode, hash, token] {
                rep->deleteLater();
                if (token != m_token || m_pendingKey != hash) return;
                const int status = rep->attribute(
                                           QNetworkRequest::
                                               HttpStatusCodeAttribute)
                                       .toInt();
                const QJsonObject body =
                    QJsonDocument::fromJson(rep->readAll()).object();
                if (status < 200 || status >= 300) {
                    if (status == 401 || status == 403)
                        finishFail(QStringLiteral("Error"), token);
                    else
                        finishFail(QStringLiteral("Stale"), token);
                    return;
                }
                if (body.value(QStringLiteral("status"))
                        .toString()
                        .compare(QLatin1String("error"),
                                 Qt::CaseInsensitive) == 0) {
                    const QString msg =
                        (body.value(QStringLiteral("message")).toString() +
                         u' ' +
                         body.value(QStringLiteral("code")).toString())
                            .toLower();
                    switch (classifyPremiumizeError(status, msg)) {
                    case PremiumizeFailure::Error:
                        finishFail(QStringLiteral("Error"), token);
                        return;
                    case PremiumizeFailure::NotCached:
                        finishFail(QStringLiteral("NotCached"), token);
                        return;
                    case PremiumizeFailure::Stale:
                        finishFail(QStringLiteral("Stale"), token);
                        return;
                    }
                }
                QList<TorrentFile> files;
                QStringList links;   // aligned with `files` by position
                for (const QJsonValue& cv :
                     body.value(QStringLiteral("content")).toArray()) {
                    const QJsonObject co = cv.toObject();
                    const QString link =
                        co.value(QStringLiteral("link")).toString();
                    const QString path =
                        co.value(QStringLiteral("path")).toString();
                    if (path.isEmpty() || link.isEmpty()) continue;
                    TorrentFile f;
                    f.id = files.size();
                    f.name = path;
                    f.size = static_cast<qint64>(
                        co.value(QStringLiteral("size")).toDouble(0));
                    files.append(f);
                    links.append(link);
                }
                const auto picked =
                    selectTorrentFile(files, {}, season, episode);
                if (!picked || picked->id < 0 ||
                    picked->id >= links.size()) {
                    finishFail(QStringLiteral("Stale"), token);
                    return;
                }
                finishOk(links[picked->id], picked->name,
                         QStringLiteral("premiumize"), token);
            });
}

void DebridResolver::finishOk(const QString& url, const QString& filename,
                              const QString& providerId, quint64 token)
{
    if (token != m_token) return;
    emit resolved(url, filename, providerId);
}

void DebridResolver::finishFail(const QString& reason, quint64 token)
{
    if (token != m_token) return;
    emit unavailable(reason);
}

} // namespace nuvio::debrid
