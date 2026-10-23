#include "gpu_surface.h"

// iOS 的 GPU 后端：Metal（CAMetalLayer + Ganesh Metal）。
// 平台只提供 CAMetalLayer，device/queue/绘制/present 全部在 core 内完成。
#if defined(__APPLE__) && defined(SKC_USE_METAL)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <skia/core/SkColorSpace.h>
#include <skia/core/SkSurface.h>
#include <skia/gpu/ganesh/GrBackendSurface.h>
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/SkSurfaceGanesh.h>
#include <skia/gpu/ganesh/mtl/GrMtlBackendContext.h>
#include <skia/gpu/ganesh/mtl/GrMtlBackendSurface.h>
#include <skia/gpu/ganesh/mtl/GrMtlDirectContext.h>
#include <skia/gpu/ganesh/mtl/GrMtlTypes.h>

#include "painter.h"

namespace skc {
namespace {

class MetalGpuSurface final : public GpuSurface {
public:
    bool attach(void *native_layer) override {
        if (context_ != nullptr) {
            return true;
        }
        CAMetalLayer *layer = (__bridge CAMetalLayer *)native_layer;
        if (layer == nil) {
            return false;
        }
        layer_ = layer;
        device_ = MTLCreateSystemDefaultDevice();
        if (device_ == nil) {
            return false;
        }
        queue_ = [device_ newCommandQueue];
        if (queue_ == nil) {
            return false;
        }
        layer.device = device_;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        // Skia 只把 drawable 当 render target 使用
        layer.framebufferOnly = YES;
        layer.opaque = YES;

        GrMtlBackendContext backend;
        backend.fDevice.retain((__bridge GrMTLHandle)device_);
        backend.fQueue.retain((__bridge GrMTLHandle)queue_);
        context_ = GrDirectContexts::MakeMetal(backend);
        return context_ != nullptr;
    }

    bool render(uint32_t fbo_id,
                int32_t width,
                int32_t height,
                const Scene &scene,
                float dpr) override {
        (void)fbo_id; // Metal 使用 drawable，无 FBO 概念
        if (context_ == nullptr || layer_ == nil || width <= 0 || height <= 0) {
            return false;
        }
        const CGSize drawable_size = CGSizeMake(width, height);
        if (!CGSizeEqualToSize(layer_.drawableSize, drawable_size)) {
            layer_.drawableSize = drawable_size;
        }
        id<CAMetalDrawable> drawable = [layer_ nextDrawable];
        if (drawable == nil) {
            // 窗口不可见等，保持上一帧
            return false;
        }

        GrMtlTextureInfo texture_info;
        texture_info.fTexture.retain((__bridge GrMTLHandle)drawable.texture);
        const GrBackendRenderTarget target =
            GrBackendRenderTargets::MakeMtl(width, height, texture_info);
        if (!target.isValid()) {
            return false;
        }
        // Metal 的纹理原点在左上角
        sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
            context_.get(), target, kTopLeft_GrSurfaceOrigin, kBGRA_8888_SkColorType, nullptr,
            nullptr);
        if (surface == nullptr) {
            return false;
        }

        Paint(surface->getCanvas(), scene, dpr);
        // 先把 Skia 的绘制命令提交到 queue，再用同一个 queue present：
        // Metal 同一队列内的 command buffer 按提交顺序执行，保证画面已完成。
        context_->flushAndSubmit(surface.get());

        id<MTLCommandBuffer> command_buffer = [queue_ commandBuffer];
        if (command_buffer == nil) {
            return false;
        }
        [command_buffer presentDrawable:drawable];
        [command_buffer commit];
        return true;
    }

    void detach() override {
        context_.reset();
        layer_ = nil;
        queue_ = nil;
        device_ = nil;
    }

private:
    CAMetalLayer *layer_ = nil;
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    sk_sp<GrDirectContext> context_;
};

} // namespace

std::unique_ptr<GpuSurface> CreateGpuSurface() {
    return std::make_unique<MetalGpuSurface>();
}

} // namespace skc

#endif // __APPLE__ && SKC_USE_METAL