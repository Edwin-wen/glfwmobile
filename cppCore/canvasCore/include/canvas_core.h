/*
 * canvas_core —— 跨端唯一绘制契约。
 *
 * 约定：
 * 1. 本头文件只暴露 C 类型与不透明指针，平台侧不需要也不允许 include Skia 头文件。
 *    这样 Android(NDK) / iOS(ObjC++) / 鸿蒙(NAPI) 三端链接的是同一份实现与同一份接口。
 * 2. 所有坐标使用「逻辑像素」（dp / pt / vp / CSS px），dpr 由 skc_set_size 传入，
 *    由 canvas_core 负责换算成物理像素，保证五端 scene 语义一致。
 * 3. 非线程安全：同一 skc_canvas 的所有调用必须由调用方串行化（各端都在 UI 线程调用）。
 */
#ifndef CANVAS_CORE_H
#define CANVAS_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct skc_canvas skc_canvas;

/** 绘制后端：raster 走 CPU 光栅化，gpu 走 Skia Ganesh（EGL / EAGL） */
typedef enum skc_backend {
    SKC_BACKEND_RASTER = 0,
    SKC_BACKEND_GPU = 1,
    SKC_BACKEND_AUTO = 2,
} skc_backend;

/** 创建画布，失败返回 NULL（默认后端为 AUTO：GPU 可用则用 GPU，否则回退 raster） */
skc_canvas *skc_create(void);

/** 销毁画布，允许传 NULL */
void skc_destroy(skc_canvas *canvas);

/**
 * 设置期望的后端。RASTER 会立即切回 CPU；GPU/AUTO 需要平台随后调用
 * skc_attach_gl（且当前线程已绑定 GL 上下文），失败会自动回退 raster。
 */
void skc_set_backend(skc_canvas *canvas, skc_backend backend);

/** 实际生效的后端（GPU attach 失败时会返回 SKC_BACKEND_RASTER） */
skc_backend skc_get_backend(const skc_canvas *canvas);

/** 实际生效的后端名："raster" / "gpu"，便于各端日志与 UI 展示 */
const char *skc_backend_name(const skc_canvas *canvas);

/**
 * 设置物理尺寸与像素密度。width/height 为物理像素，dpr 用于把 scene 的逻辑坐标
 * 换算到物理像素（dpr <= 0 时按 1.0 处理）。
 */
void skc_set_size(skc_canvas *canvas, int32_t width, int32_t height, float dpr);

/**
 * 设置场景（display list）。scene_json 为 UTF-8 JSON，编码规则见
 * `shared/src/commonMain/kotlin/com/edwin/glfwmobile/canvas/CanvasScene.kt`。
 * 解析失败时保留上一帧场景，可通过 skc_last_error 拿到原因。
 */
void skc_set_scene(skc_canvas *canvas, const char *scene_json);

/** 便捷入口：等价于只包含一个 clear op 的场景。颜色为 0xAARRGGBB。 */
void skc_set_clear_color(skc_canvas *canvas, uint32_t argb);

/**
 * CPU 光栅化到调用方内存（RGBA8888, premultiplied）。
 * pixels 必须至少有 height * row_bytes 字节；row_bytes >= width * 4。
 * 返回 false 表示尺寸未设置或参数非法。
 */
bool skc_render_raster(skc_canvas *canvas,
                       void *pixels,
                       int32_t row_bytes,
                       int32_t width,
                       int32_t height);

/**
 * 创建/复用 GPU 绘制上下文。调用线程必须已绑定 GL 上下文（Android/鸿蒙为
 * EGLContext 已 makeCurrent，iOS 为 EAGLContext 已 setCurrentContext）。
 * 返回 false 表示 GPU 不可用（调用方应继续走 skc_render_raster）。
 */
bool skc_attach_gl(skc_canvas *canvas);

/**
 * 用 GPU 直接绘制到指定帧缓冲（通常是 EGLSurface 的默认 FBO 0，iOS 为自建 FBO）。
 * width/height 为物理像素；返回 false 表示未 attach 或绘制失败，调用方需回退 raster。
 */
bool skc_render_gl(skc_canvas *canvas, uint32_t fbo_id, int32_t width, int32_t height);

/**
 * iOS：把 CAEAGLLayer 交给 canvas_core，由 core 创建 EAGLContext / renderbuffer / FBO
 * 并负责 present。其它平台返回 false。
 */
bool skc_attach_eagl(skc_canvas *canvas, void *ca_eagl_layer);

/** iOS：用 EAGL 后端绘制一帧并 present；等价于 skc_render_gl(canvas, 0, w, h) */
bool skc_render_eagl(skc_canvas *canvas, int32_t width, int32_t height);

/**
 * iOS：把 CAMetalLayer 交给 canvas_core，由 core 创建 MTLDevice / CommandQueue /
 * GrDirectContext 并负责 present。其它平台返回 false。
 */
bool skc_attach_metal(skc_canvas *canvas, void *ca_metal_layer);

/** iOS：用 Metal 后端绘制一帧并 present */
bool skc_render_metal(skc_canvas *canvas, int32_t width, int32_t height);

/**
 * 释放 GPU 上下文，后续调用 skc_render_gl 会返回 false。
 * 调用线程必须已绑定该画布的 GL 上下文；Skia 会在返回前回收自己的 GPU 资源。
 */
void skc_detach_gpu(skc_canvas *canvas);

/**
 * 放弃 GPU 上下文：不调用任何图形 API，只丢引用，允许在上下文已销毁/不可用的线程上调用。
 * Surface 销毁阶段拿不到合法 GL 现场时用它，避免 Skia 析构里的 flush/submit 卡死驱动。
 */
void skc_abandon_gpu(skc_canvas *canvas);

/** 最近一次错误信息；无错误时返回空串（不返回 NULL） */
const char *skc_last_error(const skc_canvas *canvas);

#ifdef __cplusplus
}
#endif

#endif /* CANVAS_CORE_H */