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
    connect(&settings, &nuvio::settings::AppSettings::subtitleStyleChanged,
            this, &PreferencesApplier::applySubtitles);
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
    applyCache();
    applySubtitles();
}

/// Subtitle appearance -> mpv live properties (desktop parity: font size in
/// the 6..40sp desktop range; text color as #RRGGBBAA).
void PreferencesApplier::applySubtitles()
{
    auto* ctrl = qobject_cast<nuvio::mpv::MpvController*>(m_mpv);
    if (!ctrl) return;
    ctrl->setPropertyString(QStringLiteral("sub-font-size"),
                            QString::number(m_settings->subtitleFontSize()));
    ctrl->setPropertyString(QStringLiteral("sub-color"),
                            m_settings->subtitleTextColor());
    ctrl->setPropertyString(QStringLiteral("sub-back-color"),
                            m_settings->subtitleBackgroundColor());
    ctrl->setPropertyString(QStringLiteral("sub-outline-color"),
                            m_settings->subtitleOutlineColor());
    // mpv 0.41 removed the numeric border-style values; the choice name is
    // the only accepted spelling. The disabled case is handled by forcing
    // sub-outline-size to 0 below (mpv's default style is the same choice).
    ctrl->setPropertyString(QStringLiteral("sub-border-style"),
                            QStringLiteral("outline-and-shadow"));
    ctrl->setPropertyString(QStringLiteral("sub-outline-size"),
        QString::number(m_settings->subtitleOutlineEnabled()
                            ? m_settings->subtitleOutlineWidth() : 0));
    ctrl->setPropertyString(QStringLiteral("sub-bold"),
        m_settings->subtitleBold() ? QStringLiteral("yes")
                                   : QStringLiteral("no"));
    ctrl->setPropertyString(QStringLiteral("sub-margin-y"),
        QString::number(m_settings->subtitleBottomOffset()));
}

/// Compose parity rule (AGENTS.md): forward buffer = user setting;
/// back buffer is ADDITIONAL to it, so it must be a small fraction or the
/// configured cache silently doubles.
void PreferencesApplier::applyCache()
{
    auto* ctrl = qobject_cast<nuvio::mpv::MpvController*>(m_mpv);
    if (!ctrl) return;

    const qint64 forwardBytes =
        qint64(m_settings->cacheMb()) * 1024 * 1024;
    const qint64 kFloor  = qint64(8)  * 1024 * 1024;
    const qint64 kCeil   = qint64(64) * 1024 * 1024;
    qint64 backBytes     = forwardBytes / 4;
    backBytes            = qBound(kFloor, backBytes, kCeil);

    ctrl->setPropertyString(QStringLiteral("demuxer-max-bytes"),
                            QString::number(forwardBytes));
    ctrl->setPropertyString(QStringLiteral("demuxer-max-back-bytes"),
                            QString::number(backBytes));
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