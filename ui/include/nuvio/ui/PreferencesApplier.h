#pragma once

// Preferences->player applier (ui layer: sanctioned to know BOTH settings
// and mpv - systems/* never talk sideways on their own).
//
// Directive compliance (plan W1): this is pure CONFIGURATION - we set mpv
// options per user choice; nothing here touches frame timing/pacing. The
// hardware path mapping mirrors HwdecPolicy vocabulary so the runtime
// fallback chain inside mpv still owns degradation.
//
//   auto     -> vendor-gated zero-copy first (app default, see HwdecPolicy)
//   vaapi    -> vaapi,vaapi-copy
//   nvdec    -> nvdec,nvdec-copy
//   software -> no  (CPU decode AND CPU render guard-rail for broken GL)

#include <QObject>
#include <QString>

namespace nuvio::settings {
class AppSettings;
}

namespace nuvio::mpv {
class MpvController;
}

namespace nuvio::ui {

class PreferencesApplier final : public QObject {
    Q_OBJECT
public:
    PreferencesApplier(nuvio::settings::AppSettings& settings,
                       QObject* mpvControllerObj, QObject* parent = nullptr);

    /// Push current persisted values into the running core (startup).
    void applyAll();

private slots:
    void applyDecoder();
    void applySubtitles();
    void applyCache();
    void applyResize();

private:
    [[nodiscard]] QString mappedHwdec() const;

    nuvio::settings::AppSettings* m_settings = nullptr;
    QObject*                      m_mpv      = nullptr;
};

} // namespace nuvio::ui