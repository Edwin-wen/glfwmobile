#include "gpu_surface.h"

// iOS 使用 EAGL 实现（gpu_surface_eagl.mm），本文件只服务 Android / 鸿蒙的 EGL。
#if !defined(__APPLE__)

#include <GLES3/gl3.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkSurface.h>
#include <skia/gpu/ganesh/GrBackendSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/SkSurfaceGanesh.h>
#include <skia/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <skia/gpu/ganesh/gl/GrGLDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLInterface.h>
#include <skia/gpu/ganesh/gl/GrGLTypes.h>
#include <skia/gpu/ganesh/gl/egl/GrGLMakeEGLInterface.h>

#include "painter.h"

namespace skc {
namespace {

/**
 * Ganesh + GL(EGL) 表面。
 *
 * 平台已经把窗口绑定到 EGL 上下文并 makeCurrent，这里只把默认帧缓冲（FBO 0）
 * 包成 SkSurface，绘制完成后 flush 给驱动；swap 仍由平台负责。
 */
class GLGpuSurface final : public GpuSurface {
public:
    ~GLGpuSurface() override {
        // 析构路径按「上下文可能已不可用」处理：不发起任何 GL 调用。
        // 正常释放请走 skc_detach_gpu（要求调用线程已绑定 GL 上下文）。
        abandon();
    }

    bool attach(void *native_layer) override {
        (void)native_layer;
        if (context_ != nullptr) {
            return true;
        }
        auto interface = GrGLInterfaces::MakeEGL();
        if (interface == nullptr) {
            return false;
        }
        context_ = GrDirectContexts::MakeGL(std::move(interface));
        return context_ != nullptr;
    }

    bool render(uint32_t fbo_id,
                int32_t width,
                int32_t height,
                const Scene &scene,
                float dpr) override {
        if (context_ == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        if (surface_ == nullptr || fbo_id != fbo_id_ || width != width_ || height != height_) {
            GrGLFramebufferInfo framebuffer_info{};
            framebuffer_info.fFBOID = fbo_id;
            framebuffer_info.fFormat = GL_RGBA8;
            const GrBackendRenderTarget target =
                GrBackendRenderTargets::MakeGL(width, height, 0, 8, framebuffer_info);
            if (!target.isValid()) {
                return false;
            }
            surface_ = SkSurfaces::WrapBackendRenderTarget(context_.get(), target,
                                                           kBottomLeft_GrSurfaceOrigin,
                                                           kRGBA_8888_SkColorType, nullptr,
                                                           nullptr);
            if (surface_ == nullptr) {
                return false;
            }
            fbo_id_ = fbo_id;
            width_ = width;
            height_ = height;
        }

        Paint(surface_->getCanvas(), scene, dpr);
        // 静态画面：一帧一次提交，不做常驻刷新
        context_->flushAndSubmit(surface_.get());
        return true;
    }

    void detach() override {
        if (context_ != nullptr) {
            // GrDirectContext 默认析构会 flush + submit 未提交的绘制。Surface/EGL 即将销毁时
            // 这一步会落到不合法的 GL 现场上，直接卡死驱动（APP_INPUT_BLOCK）。
            // releaseResourcesAndAbandonContext 在上下文仍可用时回收资源并放弃后续 backend 调用。
            context_->releaseResourcesAndAbandonContext();
        }
        ReleaseSkiaHandles();
    }

    void abandon() override {
        if (context_ != nullptr) {
            // 上下文已丢失/不可用：只打 abandon 标记，保证析构不再发起任何 GL 调用。
            context_->abandonContext();
        }
        ReleaseSkiaHandles();
    }

private:
    /** Surface 持有 Context 引用，必须先于 Context 释放 */
    void ReleaseSkiaHandles() {
        surface_.reset();
        context_.reset();
        fbo_id_ = 0;
        width_ = 0;
        height_ = 0;
    }
    sk_sp<GrDirectContext> context_;
    sk_sp<SkSurface> surface_;
    uint32_t fbo_id_ = 0;
    int32_t width_ = 0;
    int32_t height_ = 0;
};

} // namespace

std::unique_ptr<GpuSurface> CreateGpuSurface() {
    return std::make_unique<GLGpuSurface>();
}

} // namespace skc

#endif // !defined(__APPLE__)