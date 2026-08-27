#include "nuvio/ui/PreferencesApplier.h"

#include <nuvio/mpv/MpvController.h>
#include <nuvio/settings/AppSettings.h>

namespace nuvio::ui {

PreferencesApplier::PreferencesApplier(
    nuvio::settings::AppSettings& settings, QObject* mpvControllerObj,
    QObject* parent)
    : QObject(parent),
      m_settings(&settings),
      m_mpv(mpvControllerObj)
{
    connect(&settings, &nuvio::settings::AppSettings::decoderModeChanged,
            this, &PreferencesApplier::applyDecoder);
}

QString PreferencesApplier::mappedHwdec() const
{
    const QString mode = m_settings->decoderMode();
    if (mode == QLatin1String("vaapi"))    return QStringLiteral("vaapi,vaapi-copy");
    if (mode == QLatin1String("nvdec"))    return QStringLiteral("nvdec,nvdec-copy");
    if (mode == QLatin1String("software")) return QStringLiteral("no");
    return {};   // auto: controller's vendor-gated default already applies
}

void PreferencesApplier::applyAll()
{
    applyDecoder();
}

void PreferencesApplier::applyDecoder()
{
    auto* ctrl = qobject_cast<nuvio::mpv::MpvController*>(m_mpv);
    if (!ctrl) return;
    const QString hwdec = mappedHwdec();
    if (hwdec.isEmpty()) return;      // "auto": leave the app default alone
    ctrl->setPropertyString(QStringLiteral("hwdec"), hwdec);
    std::fprintf(stderr, "prefs: hwdec <- %s\n", qPrintable(hwdec));
}

} // namespace nuvio::ui