/*
 * GPU 表面抽象：唯一实现是 Ganesh + 原生 GL（Android/鸿蒙 EGL、iOS EAGL）。
 * 平台侧只负责"把当前 GL 上下文和帧缓冲交出来"，绘制仍然走 painter。
 */
#ifndef CANVAS_CORE_GPU_SURFACE_H
#define CANVAS_CORE_GPU_SURFACE_H

#include <cstdint>
#include <memory>

#include "scene.h"

namespace skc {

class GpuSurface {
public:
    virtual ~GpuSurface() = default;

    /** 创建 GrDirectContext；调用线程必须已绑定 GL 上下文 */
    virtual bool attach(void *native_layer) = 0;

    /** 绘制到指定帧缓冲；返回 false 时调用方回退 raster */
    virtual bool render(uint32_t fbo_id,
                        int32_t width,
                        int32_t height,
                        const Scene &scene,
                        float dpr) = 0;

    virtual void detach() = 0;
};

/** 按平台编译期选择 EGL / EAGL 接口 */
std::unique_ptr<GpuSurface> CreateGpuSurface();

} // namespace skc

#endif // CANVAS_CORE_GPU_SURFACE_H