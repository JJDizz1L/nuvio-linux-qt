#pragma once

// Provider wire layer (D1): verbatim URL/body/parse shapes for the three
// debrid providers (Torbox api.torbox.app, Real-Debrid api.real-debrid.com,
// Premiumize premiumize.me). All authed calls use `Authorization: Bearer`.
// Pure URL/body builders + tolerant response parsers (unit-tested); the
// async flows live in DebridAuth. Torrent-file resolution (RD selectFiles,
// Torbox file picking, direct download links) lands with the D2 resolver.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace nuvio::debrid {

// ---- Torbox ---------------------------------------------------------------
namespace torbox {
constexpr auto kBase = "https://api.torbox.app";

[[nodiscard]] inline QString deviceStartUrl(const QString& appName)
{
    return QString::fromLatin1(kBase) +
           QStringLiteral("/v1/api/user/auth/device/start?app=") +
           QString::fromUtf8(QUrl::toPercentEncoding(appName));
}
constexpr auto kDeviceTokenUrl =
    "https://api.torbox.app/v1/api/user/auth/device/token";
constexpr auto kUserMeUrl = "https://api.torbox.app/v1/api/user/me";
constexpr auto kCheckCachedUrl =
    "https://api.torbox.app/v1/api/torrents/checkcached?format=object";

[[nodiscard]] inline QByteArray deviceTokenBody(const QString& deviceCode)
{
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("device_code"), deviceCode},
                         })
        .toJson(QJsonDocument::Compact);
}

[[nodiscard]] inline QByteArray checkCachedBody(const QStringList& hashes)
{
    QJsonArray arr;
    for (const QString& h : hashes) {
        const QString n = h.trimmed().toLower();
        if (!n.isEmpty() && !arr.contains(n)) arr.append(n);
    }
    return QJsonDocument(
               QJsonObject{{QStringLiteral("hashes"), arr}})
        .toJson(QJsonDocument::Compact);
}

struct DeviceAuthorization {
    QString deviceCode;
    QString userCode;
    QString verificationUrl;
    int intervalSec = 5;
    QString expiresAt;
    [[nodiscard]] bool valid() const
    {
        return !deviceCode.isEmpty() && !userCode.isEmpty();
    }
};

/// Parses the envelope data object of a device-start reply.
[[nodiscard]] DeviceAuthorization parseDeviceAuthorization(
    const QByteArray& body);
/// Access token from a device-token reply envelope ("" when absent).
[[nodiscard]] QString parseDeviceToken(const QByteArray& body);
/// Envelope success flag (absent envelope counts as failure).
[[nodiscard]] bool envelopeOk(const QByteArray& body);
/// checkcached data: hash -> {name,size} (tolerant per entry).
[[nodiscard]] QMap<QString, QJsonObject> parseCheckCached(
    const QByteArray& body);
} // namespace torbox

// ---- Real-Debrid ----------------------------------------------------------
namespace realdebrid {
constexpr auto kBase = "https://api.real-debrid.com/rest/1.0";
constexpr auto kUserUrl = "https://api.real-debrid.com/rest/1.0/user";
constexpr auto kAddMagnetUrl =
    "https://api.real-debrid.com/rest/1.0/torrents/addMagnet";
constexpr auto kUnrestrictLinkUrl =
    "https://api.real-debrid.com/rest/1.0/unrestrict/link";

[[nodiscard]] inline QByteArray formBody(
    const QList<QPair<QString, QString>>& fields)
{
    QStringList parts;
    for (const auto& [k, v] : fields)
        parts.append(
            QString::fromUtf8(QUrl::toPercentEncoding(k)) + u'=' +
            QString::fromUtf8(QUrl::toPercentEncoding(v)));
    return parts.join(u'&').toUtf8();
}

struct AddedTorrent {
    QString id;
    QString uri;
};
[[nodiscard]] AddedTorrent parseAddMagnet(const QByteArray& body);
[[nodiscard]] QString parseUnrestrictedDownload(const QByteArray& body);
} // namespace realdebrid

// ---- Premiumize -----------------------------------------------------------
namespace premiumize {
constexpr auto kBase = "https://www.premiumize.me";
constexpr auto kTokenUrl = "https://www.premiumize.me/token";
constexpr auto kAccountInfoUrl =
    "https://www.premiumize.me/api/account/info";
constexpr auto kCacheCheckUrl =
    "https://www.premiumize.me/api/cache/check";

[[nodiscard]] inline QByteArray deviceStartBody(const QString& clientId)
{
    return realdebrid::formBody(
        {{"response_type", "device_code"}, {"client_id", clientId}});
}

[[nodiscard]] inline QByteArray deviceTokenBody(const QString& clientId,
                                                const QString& deviceCode)
{
    return realdebrid::formBody({{"grant_type", "device_code"},
                                 {"code", deviceCode},
                                 {"client_id", clientId}});
}

struct DeviceAuthorization {
    QString deviceCode;
    QString userCode;
    QString verificationUri;
    int intervalSec = 5;
    int expiresInSec = 600;
    [[nodiscard]] bool valid() const
    {
        return !deviceCode.isEmpty() && !userCode.isEmpty();
    }
};

[[nodiscard]] DeviceAuthorization parseDeviceAuthorization(
    const QByteArray& body);
[[nodiscard]] QString parseDeviceToken(const QByteArray& body);
/// Account-info success flag (status == "success", Compose parity).
[[nodiscard]] bool accountOk(const QByteArray& body);
/// Cache-check aligned answers (response[i] per requested item).
[[nodiscard]] QList<bool> parseCacheCheck(const QByteArray& body);
} // namespace premiumize

} // namespace nuvio::debrid
