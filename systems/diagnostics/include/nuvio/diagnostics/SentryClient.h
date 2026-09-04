#pragma once

// Sentry reporter (Appendix A, Sentry): Qt side of Compose's
// SentryInitializer (start-once, enable-driven init/close, lifecycle
// breadcrumb, 50-crumb cap, no PII). The SDK is replaced by a minimal
// envelope transport (error/message events only); native crashes are
// captured as signal/terminate markers, converted to events on the next
// launch (no minidumps/symbols until the packaging cutover wires
// sentry-native - noted in PLAN).
//
// installHooks wires process-global state (terminate handler + fatal
// signal handlers) and must be true exactly once, in production only;
// tests leave it false.

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace nuvio::diagnostics {

class SentrySettings;

class SentryClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY
                   enabledChanged)
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit SentryClient(QString versionName, QString environment,
                          bool installHooks = false,
                          QObject* parent = nullptr);
    ~SentryClient() override;

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool supported() const;
    [[nodiscard]] bool active() const { return m_active; }

    /// Idempotent boot (initializer start() parity): applies the stored
    /// opt-in, arms the hooks, flushes pending crash markers.
    Q_INVOKABLE void start();
    Q_INVOKABLE void captureMessage(const QString& text,
                                    const QString& level = QStringLiteral(
                                        "error"));

public slots:
    void setEnabled(bool on);

signals:
    void enabledChanged();
    void activeChanged();

private:
    void applyEnabled(bool on);
    void setActive(bool on);
    void addLifecycleCrumb();
    void sendEvent(const QString& message, const QString& exceptionType,
                   const QString& exceptionValue, const QString& level);
    void postEnvelope(const QByteArray& envelope);
    void flushPendingCrashes();
    [[nodiscard]] QString dsnString() const;

    QString m_versionName;
    QString m_environment;
    bool m_installHooks = false;
    SentrySettings* m_settings = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    bool m_started = false;
    bool m_active = false;
    QStringList m_crumbTrail;   // "category: message" entries, cap below
    static constexpr int kMaxCrumbs = 50;   // setMaxBreadcrumbs parity
};

} // namespace nuvio::diagnostics
