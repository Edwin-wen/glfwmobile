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

    /** 正常释放：需要调用线程已绑定 GL 上下文，Skia 会顺带 glDelete 自己的对象 */
    virtual void detach() = 0;

    /**
     * 放弃上下文：不调用任何图形 API，只丢弃 Skia 对 GPU 资源的引用。
     * 用于上下文已丢失 / 已销毁 / 被同线程其它画布抢占，无法安全 makeCurrent 的场景；
     * 默认退回 detach()，各后端可覆盖。
     */
    virtual void abandon() { detach(); }
};

/** 按平台编译期选择 EGL / EAGL 接口 */
std::unique_ptr<GpuSurface> CreateGpuSurface();

} // namespace skc

#endif // CANVAS_CORE_GPU_SURFACE_H