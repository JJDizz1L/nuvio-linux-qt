#pragma once

// Crash-report opt-in (Appendix A, Sentry): Qt side of Compose's
// SentrySettingsRepository (enabled defaults true, shared
// "nuvio_sentry_settings" store key "enabled"). Supported means a DSN is
// configured (crashReportsSupported parity); without one the UI toggle
// stays inert.

#include <QObject>

namespace nuvio::diagnostics {

class SentrySettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY
                   enabledChanged)
    Q_PROPERTY(bool supported READ supported CONSTANT)

public:
    using QObject::QObject;

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool supported() const;

public slots:
    void setEnabled(bool on);

signals:
    void enabledChanged();
};

} // namespace nuvio::diagnostics
