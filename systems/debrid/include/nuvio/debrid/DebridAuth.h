#pragma once

// Debrid authorization (D1): API-key validation for all three providers +
// device-code flows for Torbox and Premiumize (Real-Debrid is API-key
// only, Compose parity). Keys persist per-profile via DebridSettings;
// device flows are in-memory with the pending payload mirrored into the
// settings store (Compose pendingDeviceAuthorization parity) so a restart
// can resume polling. Async throughout, single-flight per object.

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace nuvio::debrid {

class DebridSettings;

class DebridAuth final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool torboxAuthorized READ torboxAuthorized NOTIFY stateChanged)
    Q_PROPERTY(bool premiumizeAuthorized READ premiumizeAuthorized
                   NOTIFY stateChanged)
    Q_PROPERTY(bool realDebridAuthorized READ realDebridAuthorized
                   NOTIFY stateChanged)
    Q_PROPERTY(QString deviceUserCode READ deviceUserCode NOTIFY flowChanged)
    Q_PROPERTY(QString deviceVerificationUrl READ deviceVerificationUrl
                   NOTIFY flowChanged)
    Q_PROPERTY(bool deviceFlowActive READ deviceFlowActive NOTIFY flowChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY flowChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY flowChanged)
    Q_PROPERTY(QString flowProviderId READ flowProviderId NOTIFY flowChanged)

public:
    explicit DebridAuth(DebridSettings* settings, QObject* parent = nullptr);

    [[nodiscard]] bool torboxAuthorized() const;
    [[nodiscard]] bool premiumizeAuthorized() const;
    [[nodiscard]] bool realDebridAuthorized() const;
    [[nodiscard]] QString deviceUserCode() const { return m_userCode; }
    [[nodiscard]] QString deviceVerificationUrl() const
    {
        return m_verifyUrl;
    }
    [[nodiscard]] bool deviceFlowActive() const { return m_flowActive; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString errorMessage() const { return m_error; }
    [[nodiscard]] QString flowProviderId() const { return m_flowProvider; }
    /// Profile switches (P7): reloads key presence.
    Q_INVOKABLE void setProfileId(int profileId);

    /// Validates + stores an API key (any provider; Torbox/Premiumize
    /// primarily use device flow, Real-Debrid only this).
    Q_INVOKABLE void authorizeWithApiKey(const QString& providerId,
                                         const QString& apiKey);
    /// Starts a device-code flow (torbox/premiumize only).
    Q_INVOKABLE void startDeviceFlow(const QString& providerId);
    Q_INVOKABLE void cancelDeviceFlow();
    Q_INVOKABLE void signOut(const QString& providerId);

signals:
    void stateChanged();   // any authorized flag flips
    void flowChanged();    // code/url/busy/error moves

private:
    void setBusy(bool busy, const QString& error = {});
    void pollDevice(const QString& providerId, const QString& deviceCode,
                    int intervalSec, qint64 expiresAtMs, quint64 generation);
    [[nodiscard]] QString premiumizeClientId() const;

    DebridSettings* m_settings = nullptr;
    int m_profileId = 1;
    bool m_busy = false;
    QString m_error;
    bool m_flowActive = false;
    QString m_flowProvider;
    QString m_userCode;
    QString m_verifyUrl;
    quint64 m_generation = 0;
    QNetworkAccessManager* m_nam = nullptr;
};

} // namespace nuvio::debrid
