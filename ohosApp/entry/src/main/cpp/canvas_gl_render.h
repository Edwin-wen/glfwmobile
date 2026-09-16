#ifndef KUIKLY_CANVAS_GL_RENDER_H
#define KUIKLY_CANVAS_GL_RENDER_H

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <napi/native_api.h>

#include <string>

/**
 * 原生 OpenGL 画布渲染器（对应 Kuikly 侧 viewName = "CanvasGLView"）。
 *
 * 通过 XComponent(SURFACE) 提供的 Surface，用 EGL + OpenGL ES 3.0 清屏绘制纯色背景。
 * 画面为静态内容，仅在 Surface 创建/尺寸变化/颜色变化时重绘。
 */

/** 注册 XComponent 生命周期回调（在 napi module 的 Init 中调用） */
int32_t RegisterCanvasGLXComponent(OH_NativeXComponent *component);

/** 设置指定 XComponent 的画布清屏颜色，color 支持 #AARRGGBB / #RRGGBB */
void SetCanvasGLClearColor(const std::string &xComponentId, const std::string &color);

/** napi 导出：setCanvasClearColor(xComponentId: string, color: string): void */
napi_value NapiSetCanvasClearColor(napi_env env, napi_callback_info info);

/** 释放全部渲染资源 */
void ReleaseAllCanvasGLRenderers();

#endif // KUIKLY_CANVAS_GL_RENDER_H
