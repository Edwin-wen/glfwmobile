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

/** 创建画布，失败返回 NULL */
skc_canvas *skc_create(void);

/** 销毁画布，允许传 NULL */
void skc_destroy(skc_canvas *canvas);

/**
 * 设置物理尺寸与像素密度。width/height 为物理像素，dpr 用于把 scene 的逻辑坐标
 * 换算到物理像素（dpr <= 0 时按 1.0 处理）。
 */
void skc_set_size(skc_canvas *canvas, int32_t width, int32_t height, float dpr);

/**
 * 设置场景（display list）。scene_json 为 UTF-8 JSON，格式见 canvasCore/README.md。
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

/** 最近一次错误信息；无错误时返回空串（不返回 NULL） */
const char *skc_last_error(const skc_canvas *canvas);

#ifdef __cplusplus
}
#endif

#endif /* CANVAS_CORE_H */