#include "nuvio/debrid/DebridApi.h"

#include <algorithm>

namespace nuvio::debrid {

namespace {
// Torbox wraps everything in {success, data, error, detail}.
QJsonObject torboxData(const QByteArray& body, bool* ok = nullptr)
{
    const QJsonObject root =
        QJsonDocument::fromJson(body).object();
    const bool success = root.value(QStringLiteral("success")).toBool(false);
    if (ok) *ok = success;
    return root.value(QStringLiteral("data")).toObject();
}
} // namespace

namespace torbox {

DeviceAuthorization parseDeviceAuthorization(const QByteArray& body)
{
    DeviceAuthorization out;
    bool ok = false;
    const QJsonObject d = torboxData(body, &ok);
    if (!ok) return out;
    out.deviceCode = d.value(QStringLiteral("device_code")).toString();
    // Torbox names the user-facing code `code`.
    out.userCode = d.value(QStringLiteral("code")).toString();
    out.verificationUrl =
        d.value(QStringLiteral("friendly_verification_url")).toString();
    if (out.verificationUrl.isEmpty())
        out.verificationUrl =
            d.value(QStringLiteral("verification_url")).toString();
    out.intervalSec = std::max(1, d.value(QStringLiteral("interval")).toInt(5));
    out.expiresAt = d.value(QStringLiteral("expires_at")).toString();
    return out;
}

QString parseDeviceToken(const QByteArray& body)
{
    bool ok = false;
    const QJsonObject d = torboxData(body, &ok);
    if (!ok) return {};
    return d.value(QStringLiteral("access_token")).toString().trimmed();
}

bool envelopeOk(const QByteArray& body)
{
    bool ok = false;
    torboxData(body, &ok);
    return ok;
}

QMap<QString, QJsonObject> parseCheckCached(const QByteArray& body)
{
    QMap<QString, QJsonObject> out;
    bool ok = false;
    const QJsonObject data = torboxData(body, &ok);
    if (!ok) return out;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        QJsonObject row;
        row.insert(QStringLiteral("name"),
                   entry.value(QStringLiteral("name")).toString());
        row.insert(QStringLiteral("size"), QJsonValue::fromVariant(
                   entry.value(QStringLiteral("size")).toVariant()));
        row.insert(QStringLiteral("hash"),
                   entry.value(QStringLiteral("hash")).toString(it.key()));
        out.insert(it.key().toLower(), row);
    }
    return out;
}

} // namespace torbox

namespace realdebrid {

AddedTorrent parseAddMagnet(const QByteArray& body)
{
    AddedTorrent out;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    out.id = o.value(QStringLiteral("id")).toString();
    out.uri = o.value(QStringLiteral("uri")).toString();
    return out;
}

QString parseUnrestrictedDownload(const QByteArray& body)
{
    return QJsonDocument::fromJson(body)
        .object()
        .value(QStringLiteral("download"))
        .toString();
}

} // namespace realdebrid

namespace premiumize {

DeviceAuthorization parseDeviceAuthorization(const QByteArray& body)
{
    DeviceAuthorization out;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (!o.value(QStringLiteral("error")).toString().isEmpty()) return out;
    out.deviceCode = o.value(QStringLiteral("device_code")).toString();
    out.userCode = o.value(QStringLiteral("user_code")).toString();
    out.verificationUri =
        o.value(QStringLiteral("verification_uri_complete")).toString();
    if (out.verificationUri.isEmpty())
        out.verificationUri =
            o.value(QStringLiteral("verification_uri")).toString();
    out.intervalSec =
        std::max(1, o.value(QStringLiteral("interval")).toInt(5));
    out.expiresInSec = std::max(1, o.value(QStringLiteral("expires_in")).toInt(600));
    return out;
}

QString parseDeviceToken(const QByteArray& body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (!o.value(QStringLiteral("error")).toString().isEmpty()) return {};
    return o.value(QStringLiteral("access_token")).toString().trimmed();
}

bool accountOk(const QByteArray& body)
{
    return QJsonDocument::fromJson(body)
        .object()
        .value(QStringLiteral("status"))
        .toString()
        .compare(QLatin1String("success"), Qt::CaseInsensitive) == 0;
}

QList<bool> parseCacheCheck(const QByteArray& body)
{
    QList<bool> out;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (o.value(QStringLiteral("status")).toString().compare(
            QLatin1String("success"), Qt::CaseInsensitive) != 0)
        return out;
    for (const QJsonValue& v :
         o.value(QStringLiteral("response")).toArray())
        out.append(v.toBool(false));
    return out;
}

} // namespace premiumize

} // namespace nuvio::debrid
