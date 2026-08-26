#include "nuvio/mpv/MpvRenderer.h"

#include "nuvio/mpv/HwdecPolicy.h"
#include "nuvio/mpv/MpvController.h"
#include "nuvio/mpv/MpvLog.h"
#include "nuvio/mpv/MpvQuickItem.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QMetaObject>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cstring>
#include <vector>

namespace nuvio::mpv {

namespace {
/// Bridge QOpenGLContext proc lookup into libmpv's expected signature.
void* procAddrBridge(void* ctxPtr, const char* name)
{
    auto* ctx = static_cast<QOpenGLContext*>(ctxPtr);
    if (!ctx || !name) return nullptr;
    return reinterpret_cast<void*>(ctx->getProcAddress(QByteArray(name)));
}

void updateCallbackStatic(void* userData)
{
    auto* item = static_cast<MpvQuickItem*>(userData);
    // Safe from any thread; queued onto the item's thread by Qt.
    QMetaObject::invokeMethod(item, "requestUpdate", Qt::QueuedConnection);
}
} // namespace

MpvRenderer::MpvRenderer(MpvQuickItem& item) : m_item(item) {}

MpvRenderer::~MpvRenderer()
{
    destroyRenderContext();
}

bool MpvRenderer::initRenderContext(const QSize& size)
{
    Q_UNUSED(size);
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx || !m_item.controller()) return false;

    const QString vendorLower =
        QString::fromLatin1(reinterpret_cast<const char*>(
            ctx->functions()->glGetString(GL_VENDOR))).toLower();

    if (!m_vendorReported) {
        m_vendorReported = true;
        // Queued: crosses threads into the controller's event thread.
        m_item.reportGlVendorToController(vendorLower);
    }

    mpv_handle* h = m_item.controller()->handleForRenderInit();
    if (!h) return false;

    mpv_opengl_init_params init{};
    init.get_proc_address      = &procAddrBridge;
    init.get_proc_address_ctx  = ctx;

    std::vector<mpv_render_param> params{
        {MPV_RENDER_PARAM_API_TYPE,       const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init},
    };

    // VAAPI dmabuf interop needs the DRM node; NVIDIA sessions omit it
    // entirely (drmprime-overlay trap, AGENTS.md issue #9).
    if (!HwdecPolicy::nvidiaDetectedBySystem()
        && !vendorLower.contains(QLatin1String("nvidia"))) {
        const QString node = HwdecPolicy::pickDrmNode(vendorLower);
        if (!node.isEmpty()) {
#ifdef Q_OS_UNIX
            const int fd = ::open(node.toUtf8().constData(), O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                m_drmFd = fd;
                params.push_back({MPV_RENDER_PARAM_DRM_DISPLAY_V2, &m_drmFd});
            }
#endif
        }
    }

    params.push_back({MPV_RENDER_PARAM_INVALID, nullptr});

    const int rc = mpv_render_context_create(&m_ctx, h, params.data());
    if (rc != 0 || !m_ctx) {
        m_ctx = nullptr;
        qCWarning(lcNuvioMpvRender,
                  "mpv_render_context_create failed rc=%d (software decode "
                  "still possible via Compatibility Rendering toggle later)",
                  rc);
        return false;
    }

    mpv_render_context_set_update_callback(m_ctx, &updateCallbackStatic, &m_item);
    m_item.publishRenderStatsIfFirst(m_stats);   // diagnostics channel handoff

    // Cross-thread announcement (queued onto the item's thread): unblocks any
    // parked play() requests and marks the video pipeline usable.
    QMetaObject::invokeMethod(&m_item, "notifyRenderContextReady",
                              Qt::QueuedConnection);

    qCInfo(lcNuvioMpvRender, "mpv render context ready (vendor=%s)",
           vendorLower.toUtf8().constData());
    return true;
}

void MpvRenderer::destroyRenderContext()
{
    if (m_ctx) {
        // Detach BEFORE free so a late wake cannot touch freed item state.
        mpv_render_context_set_update_callback(m_ctx, nullptr, nullptr);
        mpv_render_context_free(m_ctx);
        m_ctx = nullptr;
    }
#ifdef Q_OS_UNIX
    if (m_drmFd >= 0) { ::close(m_drmFd); m_drmFd = -1; }
#endif
}

void MpvRenderer::synchronize(QQuickFramebufferObject* fbo)
{
    Q_UNUSED(fbo);

    if (!m_ctxTried) {
        m_ctxTried = true;          // one honest attempt; graceful degradation
        initRenderContext(QSize());
    }
}

void MpvRenderer::render()
{
    if (!m_ctx) return;

    QOpenGLFramebufferObject* fbo = framebufferObject();
    if (!fbo) return;

    const QSize s = fbo->size();
    mpv_opengl_fbo dst{};
    dst.fbo = static_cast<int>(fbo->handle());
    dst.w   = s.width();
    dst.h   = s.height();
    dst.internal_format = 0;

    // vo_libmpv renders bottom-up into GL consumers... empirically inverted
    // on-screen (user-verified): with QQuickFramebufferObject sampling this
    // texture, FLIP_Y=0 yields upright output. Readback-line used 1 because
    // it memcpy'd pixels; the direct-FBO path needs the opposite.
    const int flipY = 0;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &dst},
        {MPV_RENDER_PARAM_FLIP_Y,     const_cast<int*>(&flipY)},
        {MPV_RENDER_PARAM_INVALID,    nullptr},
    };

    mpv_render_context_render(m_ctx, params);
    m_stats->publishedFrames.fetch_add(1, std::memory_order_relaxed);
}

QOpenGLFramebufferObject* MpvRenderer::createFramebufferObject(const QSize& size)
{
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fmt.setTextureTarget(GL_TEXTURE_2D);
    fmt.setInternalTextureFormat(GL_RGBA8);
    return new QOpenGLFramebufferObject(size, fmt);
}

} // namespace nuvio::mpv