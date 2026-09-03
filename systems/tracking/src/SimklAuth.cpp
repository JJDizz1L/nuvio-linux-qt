#include "nuvio/tracking/SimklAuth.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>

#include "nuvio/settings/ActiveProfile.h"
#include "nuvio/settings/PropertiesStore.h"

namespace nuvio::tracking {

namespace {
constexpr auto kBase = "https://api.simkl.com";
constexpr auto kStore = "simkl_auth";

[[nodiscard]] std::string tokenKey(int profileId)
{
    return "simkl_auth_token_" + std::to_string(profileId);
}
} // namespace

SimklPinOutcome simklPinPollOutcome(const QByteArray& body,
                                    QString* accessToken)
{
    const QJsonObject o =
        QJsonDocument::fromJson(body).object();
    if (!o.value(QStringLiteral("device_code")).toString().isEmpty())
        return SimklPinOutcome::Gone;
    if (o.value(QStringLiteral("result")).toString().compare(
            QLatin1String("OK"), Qt::CaseInsensitive) == 0) {
        const QString token =
            o.value(QStringLiteral("access_token")).toString().trimmed();
        if (!token.isEmpty()) {
            if (accessToken) *accessToken = token;
            return SimklPinOutcome::Authorized;
        }
    }
    if (o.value(QStringLiteral("result")).toString().compare(
            QLatin1String("KO"), Qt::CaseInsensitive) == 0)
        return SimklPinOutcome::Pending;
    return SimklPinOutcome::Failed;
}

SimklAuth::SimklAuth(const QString& appVersion, QObject* parent)
    : QObject(parent),
      m_appVersion(appVersion),
      m_profileId(nuvio::settings::ActiveProfile::id()),
      m_nam(new QNetworkAccessManager(this))
{
    loadForProfile();
}

QString SimklAuth::clientId() const
{
    return qEnvironmentVariable("NUVIO_SIMKL_CLIENT_ID");
}

bool SimklAuth::authenticated() const { return !m_token.isEmpty(); }

void SimklAuth::setBusy(bool busy, const QString& error)
{
    m_busy = busy;
    m_error = error;
    emit flowChanged();
}

void SimklAuth::applyBaseHeaders(QNetworkRequest& req, bool authed) const
{
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("client_id", clientId().toUtf8());
    req.setRawHeader(
        "app-name",
        qEnvironmentVariable("NUVIO_SIMKL_APP_NAME", "nuvio").toUtf8());
    req.setRawHeader("app-version", m_appVersion.toUtf8());
    req.setRawHeader(
        "User-Agent",
        (qEnvironmentVariable("NUVIO_SIMKL_APP_NAME", "nuvio") + u'/' +
         m_appVersion)
            .toUtf8());
    if (authed && !m_token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
}

void SimklAuth::persist()
{
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStore));
    if (m_token.isEmpty()) store.remove(tokenKey(m_profileId));
    else store.putString(tokenKey(m_profileId), m_token.toStdString());
}

void SimklAuth::loadForProfile()
{
    m_token.clear();
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    m_error.clear();
    nuvio::settings::PropertiesStore store(
        nuvio::settings::PropertiesStore::defaultPath(kStore));
    if (const auto v = store.getString(tokenKey(m_profileId)))
        m_token = QString::fromStdString(*v);
    emit stateChanged();
    emit flowChanged();
}

void SimklAuth::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    m_profileId = profileId;
    ++m_generation;
    loadForProfile();
}

void SimklAuth::startPinFlow()
{
    if (clientId().isEmpty()) {
        setBusy(false, QStringLiteral("SIMKL client ID is not configured"));
        return;
    }
    ++m_generation;
    const quint64 generation = m_generation;
    setBusy(true);
    m_flowActive = true;
    emit flowChanged();
    QNetworkRequest req{QUrl(QString::fromLatin1(kBase) +
                             QStringLiteral("/oauth/pin"))};
    applyBaseHeaders(req, false);
    QNetworkReply* rep = m_nam->get(req);
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
                if (o.value(QStringLiteral("result"))
                        .toString()
                        .compare(QLatin1String("OK"),
                                 Qt::CaseInsensitive) != 0) {
                    m_flowActive = false;
                    setBusy(false, QStringLiteral("Bad PIN response"));
                    return;
                }
                const QString userCode =
                    o.value(QStringLiteral("user_code")).toString().trimmed();
                QString verify =
                    o.value(QStringLiteral("verification_uri")).toString();
                if (verify.isEmpty())
                    verify = o.value(QStringLiteral("verification_url"))
                                 .toString();
                const qint64 lifetimeSec =
                    std::max<qint64>(1, static_cast<qint64>(
                                            o.value(QStringLiteral(
                                                        "expires_in"))
                                                .toDouble(900.0)));
                const int interval = std::max(
                    1, o.value(QStringLiteral("interval")).toInt(5));
                if (userCode.isEmpty() || verify.isEmpty()) {
                    m_flowActive = false;
                    setBusy(false, QStringLiteral("Bad PIN response"));
                    return;
                }
                m_userCode = userCode;
                m_verifyUrl = verify;
                setBusy(false);
                pollPin(userCode, interval,
                        QDateTime::currentMSecsSinceEpoch() +
                            lifetimeSec * 1000LL,
                        generation);
            });
}

void SimklAuth::pollPin(const QString& userCode, int intervalSec,
                        qint64 expiresAtMs, quint64 generation)
{
    QTimer::singleShot(intervalSec * 1000, this,
                       [this, userCode, intervalSec, expiresAtMs,
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
                                    QStringLiteral("/oauth/pin/") +
                                    QString::fromUtf8(
                                        QUrl::toPercentEncoding(userCode)))};
                           applyBaseHeaders(req, false);
                           QNetworkReply* rep = m_nam->get(req);
                           connect(rep, &QNetworkReply::finished, this,
                                   [this, rep, userCode, intervalSec,
                                    expiresAtMs, generation] {
                                       rep->deleteLater();
                                       if (generation != m_generation ||
                                           !m_flowActive)
                                           return;
                                       if (rep->error() !=
                                           QNetworkReply::NoError) {
                                           setBusy(false, rep->errorString());
                                           return;
                                       }
                                       QString token;
                                       switch (simklPinPollOutcome(
                                           rep->readAll(), &token)) {
                                       case SimklPinOutcome::Authorized:
                                           completeWithToken(token);
                                           return;
                                       case SimklPinOutcome::Pending:
                                           pollPin(userCode, intervalSec,
                                                   expiresAtMs, generation);
                                           return;
                                       case SimklPinOutcome::Gone:
                                       case SimklPinOutcome::Failed:
                                           m_flowActive = false;
                                           m_userCode.clear();
                                           m_verifyUrl.clear();
                                           setBusy(false, QStringLiteral(
                                                              "Authorization "
                                                              "failed - try "
                                                              "again"));
                                           return;
                                       }
                                   });
                       });
}

void SimklAuth::completeWithToken(const QString& token)
{
    m_token = token;
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    persist();
    setBusy(false);
    emit stateChanged();
}

void SimklAuth::cancelPinFlow()
{
    ++m_generation;
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    setBusy(false);
}

void SimklAuth::signOut()
{
    ++m_generation;
    m_token.clear();
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    persist();   // empty token removes the key
    setBusy(false);
    emit stateChanged();
}

} // namespace nuvio::tracking
