#pragma once

// SIMKL PIN auth (T3): verbatim port of Compose SimklPinAuthorization
// (GET /oauth/pin -> result/user_code/verification_uri/expires_in; poll
// GET /oauth/pin/{code} -> device_code present means Gone, result OK +
// access_token means Authorized, KO means Pending). Headers carry
// client_id/app-name/app-version (+Bearer when authed). Token persists
// per-profile (simkl_auth store, simkl_auth_token_<profileId>). Client id
// rides NUVIO_SIMKL_CLIENT_ID (empty = inert). The PKCE browser flow needs
// deeplink handling (Appendix A) and is not implemented here.

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace nuvio::tracking {

/// PIN poll outcome (SimklPinPollResult parity).
enum class SimklPinOutcome {
    Authorized,
    Pending,
    Gone,
    Failed,
};

[[nodiscard]] SimklPinOutcome simklPinPollOutcome(const QByteArray& body,
                                                  QString* accessToken);

class SimklAuth final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY flowChanged)
    Q_PROPERTY(QString verificationUrl READ verificationUrl
                   NOTIFY flowChanged)
    Q_PROPERTY(bool flowActive READ flowActive NOTIFY flowChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY flowChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY flowChanged)

public:
    explicit SimklAuth(const QString& appVersion, QObject* parent = nullptr);

    [[nodiscard]] bool authenticated() const;
    [[nodiscard]] QString userCode() const { return m_userCode; }
    [[nodiscard]] QString verificationUrl() const { return m_verifyUrl; }
    [[nodiscard]] bool flowActive() const { return m_flowActive; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString errorMessage() const { return m_error; }
    [[nodiscard]] QString accessToken() const { return m_token; }

    Q_INVOKABLE void startPinFlow();
    Q_INVOKABLE void cancelPinFlow();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void setProfileId(int profileId);

signals:
    void stateChanged();
    void flowChanged();

private:
    void setBusy(bool busy, const QString& error = {});
    void persist();
    void loadForProfile();
    void pollPin(const QString& userCode, int intervalSec, qint64 expiresAtMs,
                 quint64 generation);
    void completeWithToken(const QString& token);
    [[nodiscard]] QString clientId() const;
    void applyBaseHeaders(QNetworkRequest& req, bool authed) const;

    QString m_appVersion;
    int m_profileId = 1;
    QString m_token;
    bool m_busy = false;
    QString m_error;
    bool m_flowActive = false;
    QString m_userCode;
    QString m_verifyUrl;
    quint64 m_generation = 0;
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace nuvio::tracking
