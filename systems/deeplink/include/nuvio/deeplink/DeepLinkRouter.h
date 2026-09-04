#pragma once

// Deeplink router (Appendix A, deeplinks): QObject entry point mirroring
// Compose's handleAppUrl + AppDeepLinkRepository consumption. Tracking
// auth callbacks are a no-op set on this line (device-code/PIN flows have
// no browser-redirect leg; SIMKL ignores callbacks on desktop upstream
// too), so every app url routes straight to the pending-link signals.

#include <QObject>
#include <QString>

namespace nuvio::deeplink {

class DeepLinkRouter final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    /// Trims, scheme-filters (nuvio/stremio) and emits exactly one signal
    /// per recognized link; unrecognized input is silently ignored.
    Q_INVOKABLE void handleUrl(const QString& url);

signals:
    void openMeta(const QString& type, const QString& id);
    void installAddon(const QString& manifestUrl);
    void openDownloads();
    /// Transient feedback (addon check toasts ride with the page status).
    void notice(const QString& message);
};

} // namespace nuvio::deeplink
