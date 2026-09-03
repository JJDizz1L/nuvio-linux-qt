#pragma once

// Email/password auth against the upstream Supabase backend. Skeleton
// scope: sign-up, sign-in, refresh, persisted session via PropertiesStore
// ("auth" store, byte-compatible with the Compose line's storage dir).
//
// Threading: all I/O on the caller's thread through QNetworkAccessManager's
// internal pool — the UI thread never blocks; QObject signal results.
// Server identity strings stay upstream-exact (see AuthConfig note).

#include <QByteArray>
#include <QObject>
#include <QString>

#include "nuvio/authsync/AuthConfig.h"

class QNetworkAccessManager;

namespace nuvio::settings {
class PropertiesStore;
}

namespace nuvio::authsync {

class AuthService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY stateChanged)
    Q_PROPERTY(QString userEmail READ userEmail NOTIFY stateChanged)
    Q_PROPERTY(bool configured READ configured CONSTANT)

public:
    explicit AuthService(QObject* parent = nullptr);
    ~AuthService() override;

    [[nodiscard]] bool    configured() const { return m_cfg.valid(); }
    [[nodiscard]] bool    sessionActive() const { return m_active; }
    [[nodiscard]] QString userEmail() const { return m_email; }
    /// Stable GoTrue user id (persisted; "" when unknown). Profiles payload.
    [[nodiscard]] QString userId() const { return m_userId; }
    /// Current user JWT (empty when signed out). Consumed by the sync RPC
    /// client; GoTrue endpoints intentionally keep using the anon key.
    [[nodiscard]] QByteArray accessToken() const { return m_accessToken; }

    // restoreSession(): loads any stored tokens (no network). With an expired
    // access token but valid refresh token it silently re-enters online mode.
    Q_INVOKABLE void restoreSession();
    Q_INVOKABLE void signIn(const QString& email, const QString& password);
    Q_INVOKABLE void signUp(const QString& email, const QString& password);
    Q_INVOKABLE void signOut();

signals:
    void authResult(bool ok, const QString& error);   ///< for dialogs/status
    void stateChanged();                              ///< sessionActive/email

private:
    void postGoTrue(const QByteArray& url, const QByteArray& body,
                    bool isRefreshFlow);
    void handleAuthJson(const QJsonDocument& doc);
    void applyTokensFromJson(const QJsonObject& obj);
    void clearStored();
    void persistTokens();
    void setSession(bool active, const QString& email);

    AuthConfig m_cfg;
    QNetworkAccessManager* m_nam = nullptr;
    std::unique_ptr<nuvio::settings::PropertiesStore> m_store;

    bool       m_active      = false;
    QString    m_email;
    QString    m_userId;
    QByteArray m_accessToken;
    QByteArray m_refreshToken;
};

} // namespace nuvio::authsync