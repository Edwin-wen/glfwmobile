#include "gpu_surface.h"

// iOS 的 GPU 后端：EAGL + 自建 FBO。平台只提供 CAEAGLLayer，其余全部在 core 内完成，
// 这样 App 侧不需要引入 OpenGLES 头文件，也和 Android/鸿蒙共用同一套 C ABI。
#if defined(__APPLE__) && !defined(SKC_USE_METAL)

#import <OpenGLES/EAGL.h>
// iOS 26 SDK 起 ES3 头文件从 gl3.h 改名为 ES3/gl.h
#import <OpenGLES/ES3/gl.h>
#import <QuartzCore/CAEAGLLayer.h>

#include <skia/core/SkColorSpace.h>
#include <skia/core/SkSurface.h>
#include <skia/gpu/ganesh/GrBackendSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/SkSurfaceGanesh.h>
#include <skia/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <skia/gpu/ganesh/gl/GrGLDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLInterface.h>
#include <skia/gpu/ganesh/gl/ios/GrGLMakeIOSInterface.h>

#include "painter.h"

namespace skc {
namespace {

class EaglGpuSurface final : public GpuSurface {
public:
    bool attach(void *native_layer) override {
        if (context_ != nil) {
            return true;
        }
        CAEAGLLayer *eagl_layer = (__bridge CAEAGLLayer *)native_layer;
        if (eagl_layer == nil) {
            return false;
        }
        layer_ = eagl_layer;
        context_ = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        if (context_ == nil || ![EAGLContext setCurrentContext:context_]) {
            return false;
        }
        glGenFramebuffers(1, &framebuffer_);
        glGenRenderbuffers(1, &color_renderbuffer_);

        auto interface = GrGLInterfaces::MakeIOS();
        if (interface == nullptr) {
            return false;
        }
        context_gr_ = GrDirectContexts::MakeGL(std::move(interface));
        return context_gr_ != nullptr;
    }

    bool render(uint32_t fbo_id,
                int32_t width,
                int32_t height,
                const Scene &scene,
                float dpr) override {
        (void)fbo_id; // EAGL 使用自建 FBO
        if (context_gr_ == nullptr || context_ == nil || width <= 0 || height <= 0) {
            return false;
        }
        if (![EAGLContext setCurrentContext:context_]) {
            return false;
        }
        if (!EnsureRenderbuffer(width, height)) {
            return false;
        }
        if (surface_ == nullptr || width != width_ || height != height_) {
            GrGLFramebufferInfo framebuffer_info{};
            framebuffer_info.fFBOID = framebuffer_;
            framebuffer_info.fFormat = GL_RGBA8;
            const GrBackendRenderTarget target =
                GrBackendRenderTargets::MakeGL(width, height, 0, 8, framebuffer_info);
            if (!target.isValid()) {
                return false;
            }
            surface_ = SkSurfaces::WrapBackendRenderTarget(context_gr_.get(), target,
                                                           kBottomLeft_GrSurfaceOrigin,
                                                           kRGBA_8888_SkColorType, nullptr,
                                                           nullptr);
            if (surface_ == nullptr) {
                return false;
            }
            width_ = width;
            height_ = height;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        Paint(surface_->getCanvas(), scene, dpr);
        context_gr_->flushAndSubmit(surface_.get());
        return [context_ presentRenderbuffer:GL_RENDERBUFFER];
    }

    void detach() override {
        if (context_ != nil) {
            [EAGLContext setCurrentContext:context_];
            // 与 GL 后端同理：GrDirectContext 默认析构会 flush，必须在 EAGLContext 仍 current 时放弃
            if (context_gr_ != nullptr) {
                context_gr_->releaseResourcesAndAbandonContext();
            }
            surface_.reset();
            context_gr_.reset();
            if (framebuffer_ != 0) {
                glDeleteFramebuffers(1, &framebuffer_);
                framebuffer_ = 0;
            }
            if (color_renderbuffer_ != 0) {
                glDeleteRenderbuffers(1, &color_renderbuffer_);
                color_renderbuffer_ = 0;
            }
            [EAGLContext setCurrentContext:nil];
        } else {
            surface_.reset();
            context_gr_.reset();
        }
        context_ = nil;
        layer_ = nil;
        width_ = 0;
        height_ = 0;
    }

private:
    bool EnsureRenderbuffer(int32_t width, int32_t height) {
        if (renderbuffer_width_ == width && renderbuffer_height_ == height) {
            return true;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, color_renderbuffer_);
        if (![context_ renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer_]) {
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  color_renderbuffer_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            return false;
        }
        GLint actual_width = 0;
        GLint actual_height = 0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &actual_width);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &actual_height);
        renderbuffer_width_ = actual_width;
        renderbuffer_height_ = actual_height;
        return true;
    }

    EAGLContext *context_ = nil;
    CAEAGLLayer *layer_ = nil;
    GLuint framebuffer_ = 0;
    GLuint color_renderbuffer_ = 0;
    GLint renderbuffer_width_ = 0;
    GLint renderbuffer_height_ = 0;
    sk_sp<GrDirectContext> context_gr_;
    sk_sp<SkSurface> surface_;
    int32_t width_ = 0;
    int32_t height_ = 0;
};

} // namespace

std::unique_ptr<GpuSurface> CreateGpuSurface() {
    return std::make_unique<EaglGpuSurface>();
}

} // namespace skc

#endif // __APPLE__ && !SKC_USE_METAL