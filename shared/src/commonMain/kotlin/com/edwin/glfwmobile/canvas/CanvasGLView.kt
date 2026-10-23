package com.edwin.glfwmobile.canvas

import com.tencent.kuikly.core.base.Attr
import com.tencent.kuikly.core.base.DeclarativeBaseView
import com.tencent.kuikly.core.base.ViewContainer
import com.tencent.kuikly.core.base.event.Event

/**
 * Skia 画布视图（Kuikly 侧组件契约）。
 *
 * 业务层只声明「画什么」（[CanvasScene]），绘制由统一实现承接：
 * - 原生三端（Android / iOS / 鸿蒙）：`cppCore/canvasCore`（C++ / Skia，一份代码三端复用）
 * - Web 两端（H5 / 微信小程序）：CanvasKit（Skia 的 WASM 构建）
 *
 * 各端通过 [VIEW_NAME] 关联到自己的宿主壳，壳里只保留表面管理与桥接调用。
 *
 * 注意：自定义 View 跨端使用时**必须显式设置宽高**（如 `size(w, h)`），否则可能不显示。
 */
internal class CanvasGLView : DeclarativeBaseView<CanvasGLViewAttr, CanvasGLViewEvent>() {

    override fun createAttr(): CanvasGLViewAttr {
        return CanvasGLViewAttr()
    }

    override fun createEvent(): CanvasGLViewEvent {
        return CanvasGLViewEvent()
    }

    override fun viewName(): String {
        return VIEW_NAME
    }

    companion object {
        /** 组件名，必须与各端 Native 注册名/类名完全一致 */
        const val VIEW_NAME = "CanvasGLView"
    }
}

internal class CanvasGLViewAttr : Attr() {

    /**
     * 设置清屏颜色（`#AARRGGBB` 十六进制字符串，如 `#FFFF0000` 为不透明红色）。
     *
     * 兼容入口：等价于只包含一个 clear op 的 [scene]。新代码优先使用 [scene]。
     */
    fun clearColor(color: String): CanvasGLViewAttr {
        CLEAR_COLOR with color
        return this
    }

    /**
     * 设置整份绘制指令。五端共用同一份 scene，坐标单位为逻辑像素。
     */
    fun scene(scene: CanvasScene): CanvasGLViewAttr {
        SCENE with scene.json
        return this
    }

    companion object {
        const val CLEAR_COLOR = "clearColor"
        const val SCENE = "scene"
    }
}

internal class CanvasGLViewEvent : Event() {

    /**
     * 原生 GL 首次绘制完成（用于确认画布已成功渲染）
     */
    fun onReady(handler: () -> Unit) {
        register(ON_READY) {
            handler()
        }
    }

    companion object {
        const val ON_READY = "onReady"
    }
}

internal fun ViewContainer<*, *>.CanvasGLView(init: CanvasGLView.() -> Unit) {
    addChild(CanvasGLView(), init)
}
