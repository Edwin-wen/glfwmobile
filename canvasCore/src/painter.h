/*
 * painter —— 唯一的原生绘制逻辑：scene ops -> SkCanvas。
 *
 * Web 端的 CanvasKitPlayer 与本文件逐条 op 对齐，新增 op 时必须两边同时实现。
 */
#ifndef CANVAS_CORE_PAINTER_H
#define CANVAS_CORE_PAINTER_H

#include "scene.h"

class SkCanvas;

namespace skc {

/**
 * 在 canvas 上播放整份 scene。
 * 坐标按逻辑像素绘制，dpr 通过 canvas 变换换算到物理像素。
 */
void Paint(SkCanvas *canvas, const Scene &scene, float dpr);

} // namespace skc

#endif // CANVAS_CORE_PAINTER_H