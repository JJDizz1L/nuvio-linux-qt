#pragma once

// Trakt device-code auth (T2): verbatim port of Compose TraktAuthRepository's
// device flow (POST /oauth/device/code {client_id} -> user_code +
// verification_url + polling; POST /oauth/device/token {code, client_id,
// client_secret} -> 200 authorized / 400 pending / 410 expired / 418
// denied / 429 slowdown; POST /oauth/token refresh_token grant, 400
// invalidates). Tokens persist per-profile (trakt_auth store,
// trakt_auth_payload_<profileId>, legacy bare trakt_auth migrated for the
// primary profile). Client id/secret ride NUVIO_TRAKT_CLIENT_ID/_SECRET
// (empty = inert, Compose parity). Async throughout, single-flight poll
// per object; QML binds userCode/verificationUrl/busy/error.

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace nuvio::tracking {

struct TraktTokens {
    QString accessToken;
    QString refreshToken;
    QString tokenType;
    qint64 createdAtSec = 0;
    qint64 expiresInSec = 0;
};

/// Expiry with a 60 s clock-skew margin (Compose expires-or-expiring parity
/// in spirit; the exact Compose margin is not load-bearing here).
[[nodiscard]] bool traktTokensExpired(const TraktTokens& tokens,
                                      qint64 nowEpochSec);

class TraktAuth final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY stateChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY flowChanged)
    Q_PROPERTY(QString verificationUrl READ verificationUrl
                   NOTIFY flowChanged)
    Q_PROPERTY(bool flowActive READ flowActive NOTIFY flowChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY flowChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY flowChanged)

public:
    explicit TraktAuth(QObject* parent = nullptr);

    [[nodiscard]] bool authenticated() const;
    [[nodiscard]] QString userCode() const { return m_userCode; }
    [[nodiscard]] QString verificationUrl() const { return m_verifyUrl; }
    [[nodiscard]] bool flowActive() const { return m_flowActive; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString errorMessage() const { return m_error; }

    /// Starts a device-code flow for the ACTIVE profile (env creds must be
    /// set; cancels any in-flight flow). Results via flowChanged.
    Q_INVOKABLE void startDeviceFlow();
    Q_INVOKABLE void cancelDeviceFlow();
    Q_INVOKABLE void signOut();
    /// Valid bearer for API calls. Returns the cached token when fresh;
    /// otherwise kicks an async refresh and returns "" (callers skip this
    /// scrobble; the next tick retries). Empty when signed out or when
    /// client creds are missing (feature inert, Compose parity).
    [[nodiscard]] QString bearerToken();
    /// Profile switches (P7): reloads tokens for the new profile.
    Q_INVOKABLE void setProfileId(int profileId);

signals:
    void stateChanged();   // authenticated flips (registry observes)
    void flowChanged();    // userCode/url/busy/error moves

private:
    void setBusy(bool busy, const QString& error = {});
    void persist();
    void loadForProfile();
    void pollDevice(const QString& deviceCode, int intervalSec,
                    qint64 expiresAtMs, quint64 generation);
    void completeWithTokens(const TraktTokens& tokens);
    bool tryRefresh();
    [[nodiscard]] QString clientId() const;
    [[nodiscard]] QString clientSecret() const;

    int m_profileId = 1;
    TraktTokens m_tokens;
    bool m_busy = false;
    QString m_error;
    // In-flight device flow (in-memory only; a relaunch restarts it -
    // device codes expire in ~10 minutes anyway).
    bool m_flowActive = false;
    QString m_userCode;
    QString m_verifyUrl;
    quint64 m_generation = 0;
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace nuvio::tracking
