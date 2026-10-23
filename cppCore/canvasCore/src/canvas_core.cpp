#include "canvas_core.h"

#include <skia/core/SkImageInfo.h>
#include <skia/core/SkSurface.h>

#include <memory>
#include <new>
#include <string>
#include <utility>

#include "gpu_surface.h"
#include "painter.h"
#include "scene.h"

/*
 * skc_canvas 的生命周期与状态。
 *
 * 同时支持两条绘制路径：
 * - raster：skc_render_raster 画到调用方内存（Android Bitmap / iOS CGImage / 鸿蒙纹理）
 * - gpu   ：skc_attach_gl + skc_render_gl 直接画到平台提供的帧缓冲
 * 后端由 skc_set_backend 选择，GPU 不可用时自动回退 raster。
 */
struct skc_canvas {
    int32_t width = 0;
    int32_t height = 0;
    float dpr = 1.0f;
    skc::Scene scene;
    std::string error;

    skc_backend requested = SKC_BACKEND_AUTO;
    skc_backend active = SKC_BACKEND_RASTER;
    std::unique_ptr<skc::GpuSurface> gpu;
};

namespace {

void SetError(skc_canvas *canvas, const char *message) {
    if (canvas != nullptr) {
        canvas->error = message == nullptr ? "" : message;
    }
}

} // namespace

skc_canvas *skc_create(void) {
    auto *canvas = new (std::nothrow) skc_canvas();
    if (canvas == nullptr) {
        return nullptr;
    }
    // 与旧实现对齐：默认整页不透明红
    canvas->scene.ops.push_back(skc::Op{skc::OpType::kClear, 0xFFFF0000, {}, {}});
    return canvas;
}

void skc_destroy(skc_canvas *canvas) {
    if (canvas != nullptr) {
        canvas->gpu.reset();
    }
    delete canvas;
}

void skc_set_size(skc_canvas *canvas, int32_t width, int32_t height, float dpr) {
    if (canvas == nullptr) {
        return;
    }
    canvas->width = width < 0 ? 0 : width;
    canvas->height = height < 0 ? 0 : height;
    canvas->dpr = dpr <= 0 ? 1.0f : dpr;
}

void skc_set_scene(skc_canvas *canvas, const char *scene_json) {
    if (canvas == nullptr) {
        return;
    }
    skc::Scene parsed;
    if (!skc::ParseScene(scene_json, &parsed)) {
        SetError(canvas, skc::LastSceneError());
        return;
    }
    canvas->scene = std::move(parsed);
    canvas->error.clear();
}

void skc_set_clear_color(skc_canvas *canvas, uint32_t argb) {
    if (canvas == nullptr) {
        return;
    }
    canvas->scene.clear();
    canvas->scene.ops.push_back(skc::Op{skc::OpType::kClear, argb, {}, {}});
    canvas->error.clear();
}

void skc_set_backend(skc_canvas *canvas, skc_backend backend) {
    if (canvas == nullptr) {
        return;
    }
    canvas->requested = backend;
    if (backend == SKC_BACKEND_RASTER) {
        canvas->active = SKC_BACKEND_RASTER;
    } else if (canvas->gpu == nullptr) {
        // 真正的 attach 发生在 skc_attach_gl（需要平台先绑定 GL 上下文）
        canvas->active = SKC_BACKEND_RASTER;
    }
}

skc_backend skc_get_backend(const skc_canvas *canvas) {
    return canvas == nullptr ? SKC_BACKEND_RASTER : canvas->active;
}

const char *skc_backend_name(const skc_canvas *canvas) {
    return skc_get_backend(canvas) == SKC_BACKEND_GPU ? "gpu" : "raster";
}

bool skc_attach_gl(skc_canvas *canvas) {
    if (canvas == nullptr) {
        return false;
    }
    if (canvas->requested == SKC_BACKEND_RASTER) {
        canvas->active = SKC_BACKEND_RASTER;
        return false;
    }
    if (canvas->gpu == nullptr) {
        canvas->gpu = skc::CreateGpuSurface();
    }
    if (canvas->gpu == nullptr || !canvas->gpu->attach(nullptr)) {
        canvas->gpu.reset();
        canvas->active = SKC_BACKEND_RASTER;
        SetError(canvas, "gpu attach failed");
        return false;
    }
    canvas->active = SKC_BACKEND_GPU;
    canvas->error.clear();
    return true;
}

bool skc_attach_eagl(skc_canvas *canvas, void *ca_eagl_layer) {
#if defined(__APPLE__)
    if (canvas == nullptr || ca_eagl_layer == nullptr) {
        return false;
    }
    if (canvas->requested == SKC_BACKEND_RASTER) {
        canvas->active = SKC_BACKEND_RASTER;
        return false;
    }
    if (canvas->gpu == nullptr) {
        canvas->gpu = skc::CreateGpuSurface();
    }
    if (canvas->gpu == nullptr || !canvas->gpu->attach(ca_eagl_layer)) {
        canvas->gpu.reset();
        canvas->active = SKC_BACKEND_RASTER;
        SetError(canvas, "eagl attach failed");
        return false;
    }
    canvas->active = SKC_BACKEND_GPU;
    canvas->error.clear();
    return true;
#else
    (void)canvas;
    (void)ca_eagl_layer;
    return false;
#endif
}

bool skc_render_eagl(skc_canvas *canvas, int32_t width, int32_t height) {
    return skc_render_gl(canvas, 0, width, height);
}

bool skc_render_gl(skc_canvas *canvas, uint32_t fbo_id, int32_t width, int32_t height) {
    if (canvas == nullptr || canvas->active != SKC_BACKEND_GPU || canvas->gpu == nullptr) {
        return false;
    }
    if (!canvas->gpu->render(fbo_id, width, height, canvas->scene, canvas->dpr)) {
        SetError(canvas, "gpu render failed");
        return false;
    }
    return true;
}

void skc_detach_gpu(skc_canvas *canvas) {
    if (canvas == nullptr) {
        return;
    }
    if (canvas->gpu != nullptr) {
        canvas->gpu->detach();
        canvas->gpu.reset();
    }
    canvas->active = SKC_BACKEND_RASTER;
}

bool skc_render_raster(skc_canvas *canvas,
                       void *pixels,
                       int32_t row_bytes,
                       int32_t width,
                       int32_t height) {
    if (canvas == nullptr || pixels == nullptr) {
        SetError(canvas, "render: null canvas or pixels");
        return false;
    }
    if (width <= 0 || height <= 0 || row_bytes < width * 4) {
        SetError(canvas, "render: invalid size");
        return false;
    }

    const SkImageInfo info =
        SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels, static_cast<size_t>(row_bytes));
    if (!surface) {
        SetError(canvas, "render: wrap pixels failed");
        return false;
    }

    skc::Paint(surface->getCanvas(), canvas->scene, canvas->dpr);
    canvas->error.clear();
    return true;
}

const char *skc_last_error(const skc_canvas *canvas) {
    return canvas == nullptr ? "" : canvas->error.c_str();
}