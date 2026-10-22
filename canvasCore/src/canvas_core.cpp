#include "canvas_core.h"

#include <skia/core/SkImageInfo.h>
#include <skia/core/SkSurface.h>

#include <new>
#include <string>
#include <utility>

#include "painter.h"
#include "scene.h"

/*
 * skc_canvas 的生命周期与状态。
 * 阶段 1 只实现 CPU 光栅化；阶段 5 会在这里挂上 GPU 表面，
 * 但 C ABI 与平台侧调用方式保持不变。
 */
struct skc_canvas {
    int32_t width = 0;
    int32_t height = 0;
    float dpr = 1.0f;
    skc::Scene scene;
    std::string error;
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