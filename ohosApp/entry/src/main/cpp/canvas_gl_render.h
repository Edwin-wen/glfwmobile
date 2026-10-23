#ifndef KUIKLY_CANVAS_GL_RENDER_H
#define KUIKLY_CANVAS_GL_RENDER_H

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <napi/native_api.h>

#include <string>

/**
 * 画布渲染器（对应 Kuikly 侧 viewName = "CanvasGLView"）。
 *
 * 通过 XComponent(SURFACE) 提供的 Surface，用 EGL + OpenGL ES 3.0 把
 * canvas_core（C++ / Skia）光栅化出的 RGBA 缓冲上传成纹理并铺满屏幕。
 * 与 Android / iOS 共用同一份 Skia 绘制实现。
 */

/** 注册 XComponent 生命周期回调（在 napi module 的 Init 中调用） */
int32_t RegisterCanvasGLXComponent(OH_NativeXComponent *component);

/** 设置指定 XComponent 的画布清屏颜色，color 支持 #AARRGGBB / #RRGGBB */
void SetCanvasGLClearColor(const std::string &xComponentId, const std::string &color);

/** 设置指定 XComponent 的绘制指令（JSON，格式见 shared 侧 CanvasScene） */
void SetCanvasGLScene(const std::string &xComponentId, const std::string &sceneJson);

/** 设置指定 XComponent 的像素密度（XComponent 尺寸是物理像素，scene 坐标是 vp） */
void SetCanvasGLPixelRatio(const std::string &xComponentId, float ratio);

/** 设置绘制后端：0=raster 1=gpu 2=auto */
void SetCanvasGLBackend(const std::string &xComponentId, int32_t mode);

/** napi 导出 */
napi_value NapiSetCanvasClearColor(napi_env env, napi_callback_info info);
napi_value NapiSetCanvasScene(napi_env env, napi_callback_info info);
napi_value NapiSetCanvasPixelRatio(napi_env env, napi_callback_info info);
napi_value NapiSetCanvasBackend(napi_env env, napi_callback_info info);

/** 释放全部渲染资源 */
void ReleaseAllCanvasGLRenderers();

#endif // KUIKLY_CANVAS_GL_RENDER_H