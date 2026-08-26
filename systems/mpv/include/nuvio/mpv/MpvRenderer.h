// Scene-graph-side half of the video pipeline (plan §7):
//
//   UI thread            scene-graph render thread
//   ---------            ------------------------
//   MpvQuickItem  <--sync MpvRenderer (owns GL ctx + mpv render context)
//
// Frame selection stays mpv's job at stock defaults and presentation rides
// the compositor clock: the render-update callback merely schedules another
// scenegraph frame (requestUpdate). NO timer, scheduler, or pacing logic
// exists anywhere in this system — see timing-ownership directive.
#pragma once

#include <QQuickFramebufferObject>
#include <QSize>

#include <atomic>
#include <cstdint>
#include <memory>

struct mpv_render_context;

namespace nuvio::mpv {

class MpvQuickItem;

/** Diagnostics exchanged render-thread -> readers (smoke harness / debug). */
struct RenderStats {
    std::atomic<std::uint32_t> publishedFrames{0};
    std::atomic<std::int32_t>  probeLumaPermille{-1};  ///< avg Y 0..1000, -1 unknown
    std::atomic<std::int64_t>  lastProbeWallMs{0};
};

class MpvRenderer final : public QQuickFramebufferObject::Renderer {
public:
    explicit MpvRenderer(MpvQuickItem& item);
    ~MpvRenderer() override;

    void      synchronize(QQuickFramebufferObject* fbo) override;
    [[nodiscard]] std::shared_ptr<RenderStats> stats() const { return m_stats; }

protected:
    void                    render() override;
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override;

private:
    bool initRenderContext(const QSize& size);
    void destroyRenderContext();

    MpvQuickItem&                       m_item;
    mpv_render_context*                 m_ctx   = nullptr;
    int                                 m_drmFd = -1;
    bool                                m_ctxTried      = false;
    bool                                m_vendorReported = false;
    std::shared_ptr<RenderStats>        m_stats = std::make_shared<RenderStats>();
};

} // namespace nuvio::mpv
