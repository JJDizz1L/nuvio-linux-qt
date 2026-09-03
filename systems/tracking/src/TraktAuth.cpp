#include "nuvio/tracking/TraktAuth.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::tracking {

namespace {
constexpr auto kBase = "https://api.trakt.tv";
constexpr auto kStore = "trakt_auth";
constexpr auto kPayloadKey = "trakt_auth_payload";
constexpr auto kLegacyKey = "trakt_auth";
constexpr qint64 kSkewSec = 60;

[[nodiscard]] std::string profileKey(int profileId)
{
    return "trakt_auth_payload_" + std::to_string(profileId);
}
} // namespace

bool traktTokensExpired(const TraktTokens& tokens, qint64 nowEpochSec)
{
    if (tokens.accessToken.isEmpty()) return true;
    if (tokens.createdAtSec <= 0 || tokens.expiresInSec <= 0) return false;
    return nowEpochSec + kSkewSec >= tokens.createdAtSec + tokens.expiresInSec;
}

TraktAuth::TraktAuth(QObject* parent)
    : QObject(parent),
      m_profileId(nuvio::settings::ActiveProfile::id()),
      m_nam(new QNetworkAccessManager(this))
{
    loadForProfile();
}

QString TraktAuth::clientId() const
{
    return qEnvironmentVariable("NUVIO_TRAKT_CLIENT_ID");
}

QString TraktAuth::clientSecret() const
{
    return qEnvironmentVariable("NUVIO_TRAKT_CLIENT_SECRET");
}

bool TraktAuth::authenticated() const
{
    return !m_tokens.accessToken.isEmpty();
}

void TraktAuth::setBusy(bool busy, const QString& error)
{
    m_busy = busy;
    m_error = error;
    emit flowChanged();
}

void TraktAuth::persist()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStore));
    const QJsonObject o{
        {QStringLiteral("access_token"), m_tokens.accessToken},
        {QStringLiteral("refresh_token"), m_tokens.refreshToken},
        {QStringLiteral("token_type"), m_tokens.tokenType},
        {QStringLiteral("created_at"), m_tokens.createdAtSec},
        {QStringLiteral("expires_in"), m_tokens.expiresInSec},
    };
    store.putString(profileKey(m_profileId),
                    QString::fromUtf8(
                        QJsonDocument(o).toJson(QJsonDocument::Compact))
                        .toStdString());
}

void TraktAuth::loadForProfile()
{
    m_tokens = TraktTokens{};
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    m_error.clear();
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStore));
    QString raw;
    if (const auto v = store.getString(profileKey(m_profileId)))
        raw = QString::fromStdString(*v);
    else if (m_profileId == nuvio::settings::ActiveProfile::kDefault) {
        // Legacy primary-profile payload (Compose migration parity).
        if (const auto legacy = store.getString(kLegacyKey)) {
            raw = QString::fromStdString(*legacy);
            store.putString(profileKey(m_profileId), *legacy);
            store.remove(kLegacyKey);
        }
    }
    if (raw.isEmpty()) {
        emit stateChanged();
        emit flowChanged();
        return;
    }
    const QJsonObject o =
        QJsonDocument::fromJson(raw.toUtf8()).object();
    m_tokens.accessToken = o.value(QStringLiteral("access_token")).toString();
    m_tokens.refreshToken =
        o.value(QStringLiteral("refresh_token")).toString();
    m_tokens.tokenType = o.value(QStringLiteral("token_type")).toString();
    m_tokens.createdAtSec =
        static_cast<qint64>(o.value(QStringLiteral("created_at")).toDouble(0));
    m_tokens.expiresInSec =
        static_cast<qint64>(o.value(QStringLiteral("expires_in")).toDouble(0));
    emit stateChanged();
    emit flowChanged();
}

void TraktAuth::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    m_profileId = profileId;
    ++m_generation;   // abandon any in-flight device poll
    loadForProfile();
}

void TraktAuth::startDeviceFlow()
{
    if (clientId().isEmpty()) {
        setBusy(false, QStringLiteral("Trakt client ID is not configured"));
        return;
    }
    ++m_generation;
    const quint64 generation = m_generation;
    Q_UNUSED(generation);
    setBusy(true);
    m_flowActive = true;
    emit flowChanged();
    QNetworkRequest req{QUrl(QString::fromLatin1(kBase) +
                             QStringLiteral("/oauth/device/code"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* rep = m_nam->post(
        req, QJsonDocument(QJsonObject{
                   {QStringLiteral("client_id"), clientId()},
               })
                 .toJson(QJsonDocument::Compact));
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, generation] {
                rep->deleteLater();
                if (generation != m_generation) return;
                if (rep->error() != QNetworkReply::NoError) {
                    m_flowActive = false;
                    setBusy(false, rep->errorString());
                    return;
                }
                const QJsonObject o = QJsonDocument::fromJson(rep->readAll())
                                          .object();
                const QString deviceCode =
                    o.value(QStringLiteral("device_code")).toString();
                const QString userCode =
                    o.value(QStringLiteral("user_code")).toString();
                QString verify =
                    o.value(QStringLiteral("verification_url")).toString();
                if (verify.isEmpty())
                    verify =
                        o.value(QStringLiteral("verification_uri")).toString();
                const int expiresIn =
                    o.value(QStringLiteral("expires_in")).toInt(600);
                const int interval =
                    std::max(1, o.value(QStringLiteral("interval")).toInt(5));
                if (deviceCode.isEmpty() || userCode.isEmpty()) {
                    m_flowActive = false;
                    setBusy(false, QStringLiteral("Bad device response"));
                    return;
                }
                m_userCode = userCode;
                m_verifyUrl = verify;
                setBusy(false);
                const qint64 expiresAtMs =
                    QDateTime::currentMSecsSinceEpoch() +
                    qint64(std::max(1, expiresIn)) * 1000LL;
                pollDevice(deviceCode, interval, expiresAtMs, generation);
            });
}

void TraktAuth::pollDevice(const QString& deviceCode, int intervalSec,
                           qint64 expiresAtMs, quint64 generation)
{
    QTimer::singleShot(intervalSec * 1000, this,
                       [this, deviceCode, intervalSec, expiresAtMs,
                        generation] {
                           if (generation != m_generation || !m_flowActive)
                               return;
                           if (QDateTime::currentMSecsSinceEpoch() >=
                               expiresAtMs) {
                               m_flowActive = false;
                               m_userCode.clear();
                               m_verifyUrl.clear();
                               setBusy(false, QStringLiteral(
                                                  "Code expired - try again"));
                               return;
                           }
                           QNetworkRequest req{
                               QUrl(QString::fromLatin1(kBase) +
                                    QStringLiteral("/oauth/device/token"))};
                           req.setHeader(QNetworkRequest::ContentTypeHeader,
                                         "application/json");
                           req.setRawHeader("Accept", "application/json");
                           QNetworkReply* rep = m_nam->post(
                               req,
                               QJsonDocument(QJsonObject{
                                   {QStringLiteral("code"), deviceCode},
                                   {QStringLiteral("client_id"), clientId()},
                                   {QStringLiteral("client_secret"),
                                    clientSecret()},
                               }).toJson(QJsonDocument::Compact));
                           connect(rep, &QNetworkReply::finished, this,
                                   [this, rep, deviceCode, intervalSec,
                                    expiresAtMs, generation] {
                                       rep->deleteLater();
                                       if (generation != m_generation ||
                                           !m_flowActive)
                                           return;
                                       const int status = rep->attribute(
                                           QNetworkRequest::
                                               HttpStatusCodeAttribute)
                                           .toInt();
                                       const QJsonObject o =
                                           QJsonDocument::fromJson(
                                               rep->readAll())
                                               .object();
                                       if (status >= 200 && status < 300) {
                                           TraktTokens tokens;
                                           tokens.accessToken =
                                               o.value(QStringLiteral(
                                                           "access_token"))
                                                   .toString();
                                           tokens.refreshToken =
                                               o.value(QStringLiteral(
                                                           "refresh_token"))
                                                   .toString();
                                           tokens.tokenType =
                                               o.value(QStringLiteral(
                                                           "token_type"))
                                                   .toString();
                                           tokens.createdAtSec =
                                               static_cast<qint64>(
                                                   o.value(QStringLiteral(
                                                               "created_at"))
                                                       .toDouble(0));
                                           if (tokens.createdAtSec <= 0)
                                               tokens.createdAtSec =
                                                   QDateTime::currentDateTimeUtc()
                                                       .toSecsSinceEpoch();
                                           tokens.expiresInSec =
                                               static_cast<qint64>(
                                                   o.value(QStringLiteral(
                                                               "expires_in"))
                                                       .toDouble(0));
                                           if (tokens.accessToken.isEmpty()) {
                                               setBusy(false, QStringLiteral(
                                                                  "Bad token response"));
                                               return;
                                           }
                                           completeWithTokens(tokens);
                                           return;
                                       }
                                       if (status == 400) {
                                           // Pending: keep polling.
                                           pollDevice(deviceCode, intervalSec,
                                                      expiresAtMs, generation);
                                           return;
                                       }
                                       if (status == 429) {
                                           // Slow down: widen the interval.
                                           pollDevice(deviceCode,
                                                      intervalSec +
                                                          intervalSec,
                                                      expiresAtMs, generation);
                                           return;
                                       }
                                       if (status == 410) {
                                           m_flowActive = false;
                                           m_userCode.clear();
                                           m_verifyUrl.clear();
                                           setBusy(false, QStringLiteral(
                                                              "Code expired - "
                                                              "try again"));
                                           return;
                                       }
                                       if (status == 418) {
                                           m_flowActive = false;
                                           m_userCode.clear();
                                           m_verifyUrl.clear();
                                           setBusy(false, QStringLiteral(
                                                              "Authorization "
                                                              "denied"));
                                           return;
                                       }
                                       setBusy(false, rep->errorString());
                                   });
                       });
}

void TraktAuth::completeWithTokens(const TraktTokens& tokens)
{
    m_tokens = tokens;
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    persist();
    setBusy(false);
    emit stateChanged();
}

void TraktAuth::cancelDeviceFlow()
{
    ++m_generation;
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    setBusy(false);
}

void TraktAuth::signOut()
{
    ++m_generation;
    m_tokens = TraktTokens{};
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStore));
    store.remove(profileKey(m_profileId));
    setBusy(false);
    emit stateChanged();
}

QString TraktAuth::bearerToken()
{
    if (m_tokens.accessToken.isEmpty()) return {};
    const qint64 now =
        QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    if (!traktTokensExpired(m_tokens, now)) return m_tokens.accessToken;
    // Expired: best-effort async refresh, caller skips this round.
    if (!m_tokens.refreshToken.isEmpty() && !clientId().isEmpty())
        tryRefresh();
    return {};
}

bool TraktAuth::tryRefresh()
{
    if (m_busy) return false;
    setBusy(true);
    QNetworkRequest req{QUrl(QString::fromLatin1(kBase) +
                             QStringLiteral("/oauth/token"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* rep = m_nam->post(
        req,
        QJsonDocument(QJsonObject{
            {QStringLiteral("refresh_token"), m_tokens.refreshToken},
            {QStringLiteral("client_id"), clientId()},
            {QStringLiteral("client_secret"), clientSecret()},
            // Compose TraktConfig.REDIRECT_URI (default nuvio://auth/trakt).
            {QStringLiteral("redirect_uri"),
             qEnvironmentVariable("NUVIO_TRAKT_REDIRECT_URI",
                                  "nuvio://auth/trakt")},
        }).toJson(QJsonDocument::Compact));
    connect(rep, &QNetworkReply::finished, this, [this, rep] {
        rep->deleteLater();
        const int status =
            rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 400) {
            // Refresh rejected: credentials are dead, clear them.
            signOut();
            return;
        }
        if (status < 200 || status >= 300) {
            setBusy(false);   // transient: keep tokens, retry later
            return;
        }
        const QJsonObject o =
            QJsonDocument::fromJson(rep->readAll()).object();
        TraktTokens tokens = m_tokens;
        const QString access =
            o.value(QStringLiteral("access_token")).toString();
        if (access.isEmpty()) {
            setBusy(false);
            return;
        }
        tokens.accessToken = access;
        const QString refresh =
            o.value(QStringLiteral("refresh_token")).toString();
        if (!refresh.isEmpty()) tokens.refreshToken = refresh;
        tokens.tokenType = o.value(QStringLiteral("token_type")).toString();
        tokens.createdAtSec = static_cast<qint64>(
            o.value(QStringLiteral("created_at")).toDouble(0));
        if (tokens.createdAtSec <= 0)
            tokens.createdAtSec =
                QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
        tokens.expiresInSec = static_cast<qint64>(
            o.value(QStringLiteral("expires_in")).toDouble(0));
        m_tokens = tokens;
        persist();
        setBusy(false);
        emit stateChanged();
    });
    return true;
}

} // namespace nuvio::tracking
