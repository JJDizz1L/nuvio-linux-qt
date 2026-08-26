// Hardware-decode policy: vendor chain selection + DRM render-node discovery.
// Ported *doctrine*, not literals: every input arrives at runtime
// (filesystem probes, GL_VENDOR string, env override). No GPU vendor or node
// path is ever baked in.
#pragma once

#include <QString>
#include <QStringList>

namespace nuvio::mpv {

class HwdecPolicy {
public:
    /// Env escape hatch honored across the whole product line:
    /// NUVIO_MPV_HWDEC=<chain> wins over everything else when non-empty.
    [[nodiscard]] static QString userHwdecOverride();

    /// Cheap pre-GL vendor hint: NVIDIA driver presence via driver artifacts
    /// (/sys/module/nvidia or /dev/nvidiactl). Mirrors init-time detection
    /// that must run before a GL context exists.
    [[nodiscard]] static bool nvidiaDetectedBySystem();

    /**
     * Select the hwdec option value.
     * @param glVendorLower  GL_VENDOR lowercased ("" pre-context)
     * @param nvidiaViaFiles result of nvidiaDetectedBySystem()
     * Design rules ported from the Compose bridge:
     *  - NVIDIA proprietary rejects EGL alongside its GLX stack -> we never
     *    hand VAAPI-first chains to an NVIDIA GL session; nvdec first there.
     *  - Mesa/AMD+Intel favor native zero-copy vaapi with copy-mode fallback
     *    (RDNA4 AV1 export failures degrade, they don't break).
     *  - Anything unknown falls to auto-copy (always renders, never black).
     */
    [[nodiscard]] static QString selectChain(const QString& glVendorLower,
                                             bool           nvidiaViaFiles);

    /** Enumerate /dev/dri/renderD* (numeric order); empty when none exist. */
    [[nodiscard]] static QStringList enumerateDrmRenderNodes();

    /**
     * Pick one DRM render node for VAAPI interop, preferring nodes whose
     * kernel driver matches the GL device (tiered match, then lottery).
     * @param glVendorLower lowercase GL vendor; "" disables tier matching.
     * @return absolute node path or "" (caller omits the render-API param).
     */
    [[nodiscard]] static QString pickDrmNode(const QString& glVendorLower);

    /** True when the DRM DISPLAY_V2 render param should be supplied. */
    [[nodiscard]] static bool shouldPassDrmNode(bool nvidiaViaFiles)
    { return !nvidiaViaFiles; }
};

} // namespace nuvio::mpv
