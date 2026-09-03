#include "nuvio/authsync/AuthService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <nuvio/settings/PropertiesStore.h>

namespace nuvio::authsync {
namespace {

constexpr auto kAccessToken  = "access_token";
constexpr auto kRefreshToken = "refresh_token";
constexpr auto kUserEmail    = "user_email";
constexpr auto kUserId       = "user_id";   // P7: profiles payload needs it

QNetworkRequest goTrueRequest(const AuthConfig& cfg, const QByteArray& url)
{
    QNetworkRequest req{QUrl(QString::fromUtf8(url))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", cfg.anonKey);
    req.setRawHeader("Authorization", "Bearer " + cfg.anonKey);
    return req;
}

std::string toStd(const QByteArray& b)
{
    return std::string(b.constData(), size_t(b.size()));
}

} // namespace

AuthService::AuthService(QObject* parent)
    : QObject(parent),
      m_cfg(AuthConfig::load()),
      m_store(std::make_unique<nuvio::settings::PropertiesStore>(
          nuvio::settings::PropertiesStore::defaultPath("auth"))),
      m_nam(new QNetworkAccessManager(this))
{
}

AuthService::~AuthService() = default;

void AuthService::restoreSession()
{
    const auto access  = m_store->getString(kAccessToken);
    const auto refresh = m_store->getString(kRefreshToken);
    const auto email   = m_store->getString(kUserEmail);
    const auto userId  = m_store->getString(kUserId);
    if (userId && !userId->empty())
        m_userId = QString::fromStdString(*userId);
    if (!refresh || refresh->empty()) return;          // nothing stored

    if (access && !access->empty()) {
        setSession(true,
                   QString::fromStdString(email.value_or(std::string())));
        return;
    }
    m_refreshToken = QByteArray::fromStdString(refresh.value());
    postGoTrue(m_cfg.refreshUrl(),
               "{\"refresh_token\":\"" + m_refreshToken + "\"}",
               /*isRefreshFlow=*/true);
}

void AuthService::signIn(const QString& emailIn, const QString& password)
{
    if (!m_cfg.valid()) { emit authResult(false, "Supabase not configured"); return; }
    if (emailIn.isEmpty() || password.isEmpty()) {
        emit authResult(false, "Email and password required"); return;
    }
    postGoTrue(m_cfg.tokenUrl(),
               QJsonDocument(QJsonObject{{"email", emailIn},
                                         {"password", password}})
                   .toJson(QJsonDocument::Compact),
               false);
}

void AuthService::signUp(const QString& emailIn, const QString& password)
{
    if (!m_cfg.valid()) { emit authResult(false, "Supabase not configured"); return; }
    if (emailIn.isEmpty() || password.isEmpty()) {
        emit authResult(false, "Email and password required"); return;
    }
    postGoTrue(m_cfg.signupUrl(),
               QJsonDocument(QJsonObject{{"email", emailIn},
                                         {"password", password}})
                   .toJson(QJsonDocument::Compact),
               false);
}

void AuthService::signOut()
{
    clearStored();
    m_accessToken.clear();
    m_refreshToken.clear();
    setSession(false, {});
}


void AuthService::postGoTrue(const QByteArray& url, const QByteArray& body,
                             const bool isRefreshFlow)
{
    auto* rep = m_nam->post(goTrueRequest(m_cfg, url), body);
    connect(rep, &QNetworkReply::finished, this, [this, rep, isRefreshFlow] {
        rep->deleteLater();
        const QByteArray raw   = rep->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(raw);

        if (rep->error() != QNetworkReply::NoError) {
            QString msg = rep->errorString();
            const auto descr = doc.object().value("error_description");
            const auto msgV  = doc.object().value("msg");
            if (descr.isString())      msg = descr.toString();
            else if (msgV.isString())  msg = msgV.toString();
            emit authResult(false, msg);
            return;
        }

        handleAuthJson(doc);
        if (!isRefreshFlow) emit authResult(true, {});
    });
}

void AuthService::handleAuthJson(const QJsonDocument& doc)
{
    applyTokensFromJson(doc.object());

    // Sign-up can come back sessionless when confirmation mails are on:
    // surface that honestly instead of pretending a signed-in state.
    if (!m_active) {
        const bool confirmationPending =
            !doc.object().value("identity_id").toString().isEmpty() ||
            !doc.object().value("user").toObject().isEmpty();
        if (confirmationPending)
            emit authResult(true,
                            QStringLiteral(
                                "Account created - check your inbox to "
                                "confirm, then sign in"));
    }
}

void AuthService::applyTokensFromJson(const QJsonObject& obj)
{
    // Token grant responses carry tokens at the top level; signup-with-
    // session nests them under "session".
    const QJsonObject src =
        obj.contains(QLatin1String("access_token"))
            ? obj
            : obj.value(QStringLiteral("session")).toObject();

    const QString access  = src.value(QStringLiteral("access_token")).toString();
    const QString refresh = src.value(QStringLiteral("refresh_token")).toString();
    if (!access.isEmpty())  m_accessToken  = access.toUtf8();
    if (!refresh.isEmpty()) m_refreshToken = refresh.toUtf8();

    const QString mail =
        src.value("user").toObject().value("email").toString(
            obj.value("email").toString());
    if (!mail.isEmpty()) m_email = mail;
    // GoTrue nests the stable user id under user.id on both flows.
    const QString uid = src.value("user").toObject().value("id").toString(
        obj.value("user").toObject().value("id").toString());
    if (!uid.isEmpty()) m_userId = uid;

    if (!m_accessToken.isEmpty()) {
        persistTokens();
        setSession(true, m_email);
    } else {
        emit stateChanged();     // confirmation-pending: no session yet
    }
}

void AuthService::persistTokens()
{
    if (!m_accessToken.isEmpty())
        m_store->putString(kAccessToken, toStd(m_accessToken));
    if (!m_refreshToken.isEmpty())
        m_store->putString(kRefreshToken, toStd(m_refreshToken));
    if (!m_email.isEmpty())
        m_store->putString(kUserEmail, m_email.toStdString());
    if (!m_userId.isEmpty())
        m_store->putString(kUserId, m_userId.toStdString());
}

void AuthService::clearStored()
{
    m_store->remove(kAccessToken);
    m_store->remove(kRefreshToken);
    m_store->remove(kUserEmail);
    m_store->remove(kUserId);
    m_userId.clear();
}

void AuthService::setSession(const bool active, const QString& email)
{
    const bool changed = active != m_active || email != m_email;
    m_active = active;
    m_email  = email;
    if (changed) emit stateChanged();
}

} // namespace nuvio::authsync
