#include "nuvio/debrid/DebridAuth.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>

#include "nuvio/debrid/DebridApi.h"
#include "nuvio/debrid/DebridSettings.h"
#include "nuvio/settings/ActiveProfile.h"

namespace nuvio::debrid {

namespace {
constexpr auto kAppName = "nuvio";

void applyBearer(QNetworkRequest& req, const QString& apiKey)
{
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
}
} // namespace

DebridAuth::DebridAuth(DebridSettings* settings, QObject* parent)
    : QObject(parent),
      m_settings(settings),
      m_profileId(nuvio::settings::ActiveProfile::id()),
      m_nam(new QNetworkAccessManager(this))
{}

bool DebridAuth::torboxAuthorized() const
{
    return m_settings &&
           !m_settings->providerApiKey(QStringLiteral("torbox")).isEmpty();
}

bool DebridAuth::premiumizeAuthorized() const
{
    return m_settings &&
           !m_settings->providerApiKey(QStringLiteral("premiumize"))
                .isEmpty();
}

bool DebridAuth::realDebridAuthorized() const
{
    return m_settings &&
           !m_settings->providerApiKey(QStringLiteral("realdebrid"))
                .isEmpty();
}

void DebridAuth::setProfileId(int profileId)
{
    if (m_profileId == profileId) return;
    m_profileId = profileId;
    ++m_generation;
    emit stateChanged();
}

void DebridAuth::setBusy(bool busy, const QString& error)
{
    m_busy = busy;
    m_error = error;
    emit flowChanged();
}

QString DebridAuth::premiumizeClientId() const
{
    return qEnvironmentVariable("NUVIO_PREMIUMIZE_CLIENT_ID");
}

void DebridAuth::authorizeWithApiKey(const QString& providerId,
                                     const QString& apiKey)
{
    const QString key = apiKey.trimmed();
    if (key.isEmpty() || !m_settings) {
        setBusy(false, QStringLiteral("API key required"));
        return;
    }
    const QString norm = providerKeyId(providerId);
    setBusy(true);
    QString url;
    if (norm == QLatin1String("realdebrid"))
        url = QString::fromLatin1(realdebrid::kUserUrl);
    else if (norm == QLatin1String("premiumize"))
        url = QString::fromLatin1(premiumize::kAccountInfoUrl);
    else
        url = QString::fromLatin1(torbox::kUserMeUrl);
    QNetworkRequest req{QUrl(url)};
    applyBearer(req, key);
    QNetworkReply* rep = m_nam->get(req);
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, key, norm, providerId] {
                rep->deleteLater();
                if (rep->error() != QNetworkReply::NoError) {
                    setBusy(false, rep->errorString());
                    return;
                }
                const QByteArray body = rep->readAll();
                bool ok = rep->attribute(QNetworkRequest::
                                             HttpStatusCodeAttribute)
                              .toInt() >= 200 &&
                          rep->attribute(QNetworkRequest::
                                             HttpStatusCodeAttribute)
                                  .toInt() < 300;
                if (ok && norm == QLatin1String("premiumize"))
                    ok = premiumize::accountOk(body);
                if (ok && norm == QLatin1String("torbox"))
                    ok = torbox::envelopeOk(body);
                if (!ok) {
                    setBusy(false,
                            QStringLiteral("Key rejected by provider"));
                    return;
                }
                m_settings->setProviderApiKey(providerId, key);
                setBusy(false);
                emit stateChanged();
            });
}

void DebridAuth::startDeviceFlow(const QString& providerId)
{
    const QString norm = providerKeyId(providerId);
    if (norm != QLatin1String("torbox") &&
        norm != QLatin1String("premiumize")) {
        setBusy(false, QStringLiteral("Device flow needs Torbox/Premiumize"));
        return;
    }
    if (norm == QLatin1String("premiumize") &&
        premiumizeClientId().isEmpty()) {
        setBusy(false,
                QStringLiteral("Premiumize client ID is not configured"));
        return;
    }
    ++m_generation;
    const quint64 generation = m_generation;
    m_flowProvider = norm;
    m_flowActive = true;
    setBusy(true);
    if (norm == QLatin1String("torbox")) {
        QNetworkRequest req{
            QUrl(torbox::deviceStartUrl(QString::fromLatin1(kAppName)))};
        req.setRawHeader("Accept", "application/json");
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
                    const auto auth = torbox::parseDeviceAuthorization(
                        rep->readAll());
                    if (!auth.valid()) {
                        m_flowActive = false;
                        setBusy(false,
                                QStringLiteral("Bad device response"));
                        return;
                    }
                    m_userCode = auth.userCode;
                    m_verifyUrl = auth.verificationUrl;
                    setBusy(false);
                    // Torbox expiry rides expires_at (ISO); fall back to
                    // 10 minutes when unparseable (Compose 600 s default).
                    qint64 expiresAtMs =
                        QDateTime::currentMSecsSinceEpoch() + 600LL * 1000LL;
                    const QDateTime parsed = QDateTime::fromString(
                        auth.expiresAt, Qt::ISODateWithMs);
                    if (parsed.isValid())
                        expiresAtMs = parsed.toMSecsSinceEpoch();
                    pollDevice(QStringLiteral("torbox"), auth.deviceCode,
                               auth.intervalSec, expiresAtMs, generation);
                });
        return;
    }
    // Premiumize device start (form-encoded OAuth).
    QNetworkRequest req{QUrl(QString::fromLatin1(premiumize::kTokenUrl))};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* rep = m_nam->post(
        req, premiumize::deviceStartBody(premiumizeClientId()));
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, generation] {
                rep->deleteLater();
                if (generation != m_generation) return;
                if (rep->error() != QNetworkReply::NoError) {
                    m_flowActive = false;
                    setBusy(false, rep->errorString());
                    return;
                }
                const auto auth = premiumize::parseDeviceAuthorization(
                    rep->readAll());
                if (!auth.valid()) {
                    m_flowActive = false;
                    setBusy(false, QStringLiteral("Bad device response"));
                    return;
                }
                m_userCode = auth.userCode;
                m_verifyUrl = auth.verificationUri;
                setBusy(false);
                pollDevice(QStringLiteral("premiumize"), auth.deviceCode,
                           auth.intervalSec,
                           QDateTime::currentMSecsSinceEpoch() +
                               qint64(auth.expiresInSec) * 1000LL,
                           generation);
            });
}

void DebridAuth::pollDevice(const QString& providerId,
                            const QString& deviceCode, int intervalSec,
                            qint64 expiresAtMs, quint64 generation)
{
    QTimer::singleShot(intervalSec * 1000, this,
                       [this, providerId, deviceCode, intervalSec, expiresAtMs,
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
                           const bool torbox =
                               providerId == QLatin1String("torbox");
                           QNetworkRequest req{QUrl(
                               torbox ? QString::fromLatin1(
                                            torbox::kDeviceTokenUrl)
                                      : QString::fromLatin1(
                                            premiumize::kTokenUrl))};
                           req.setHeader(QNetworkRequest::ContentTypeHeader,
                                         torbox ? "application/json"
                                                : "application/x-www-form-urlencoded");
                           req.setRawHeader("Accept", "application/json");
                           const QByteArray body =
                               torbox
                                   ? torbox::deviceTokenBody(deviceCode)
                                   : premiumize::deviceTokenBody(
                                         premiumizeClientId(), deviceCode);
                           QNetworkReply* rep = m_nam->post(req, body);
                           connect(rep, &QNetworkReply::finished, this,
                                   [this, rep, torbox, deviceCode, intervalSec,
                                    expiresAtMs, generation, providerId] {
                                       rep->deleteLater();
                                       if (generation != m_generation ||
                                           !m_flowActive)
                                           return;
                                       const int status = rep->attribute(
                                           QNetworkRequest::
                                               HttpStatusCodeAttribute)
                                           .toInt();
                                       // Explicit authorization failures end
                                       // the flow; everything else (pending
                                       // envelopes, 429/5xx, transport
                                       // blips) keeps polling until expiry.
                                       if (status == 401 || status == 403 ||
                                           status == 410 || status == 418) {
                                           m_flowActive = false;
                                           m_userCode.clear();
                                           m_verifyUrl.clear();
                                           setBusy(false, QStringLiteral(
                                                              "Authorization "
                                                              "failed - try "
                                                              "again"));
                                           return;
                                       }
                                       const QByteArray raw = rep->readAll();
                                       const QString token =
                                           torbox
                                               ? torbox::parseDeviceToken(raw)
                                               : premiumize::parseDeviceToken(
                                                     raw);
                                       if (!token.isEmpty()) {
                                           if (m_settings)
                                               m_settings->setProviderApiKey(
                                                   providerId, token);
                                           m_flowActive = false;
                                           m_userCode.clear();
                                           m_verifyUrl.clear();
                                           setBusy(false);
                                           emit stateChanged();
                                           return;
                                       }
                                       pollDevice(providerId, deviceCode,
                                                  intervalSec, expiresAtMs,
                                                  generation);
                                   });
                       });
}

void DebridAuth::cancelDeviceFlow()
{
    ++m_generation;
    m_flowActive = false;
    m_userCode.clear();
    m_verifyUrl.clear();
    setBusy(false);
}

void DebridAuth::signOut(const QString& providerId)
{
    ++m_generation;
    if (m_settings) m_settings->setProviderApiKey(providerId, {});
    m_flowActive = false;
    setBusy(false);
    emit stateChanged();
}

} // namespace nuvio::debrid
