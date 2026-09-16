package com.edwin.glfwmobile.canvas

import com.tencent.kuikly.core.base.Attr
import com.tencent.kuikly.core.base.DeclarativeBaseView
import com.tencent.kuikly.core.base.ViewContainer
import com.tencent.kuikly.core.base.event.Event

/**
 * 原生 OpenGL 画布视图（Kuikly 侧组件契约）。
 *
 * Kuikly 侧只定义组件契约，真正的绘制由各端宿主用原生 OpenGL/WebGL 能力实现，
 * 通过统一的 [VIEW_NAME] 关联：
 * - Android：GLSurfaceView + OpenGL ES 2.0
 * - iOS：UIView + CAEAGLLayer/EAGLContext（类名必须为 CanvasGLView）
 * - 鸿蒙：XComponent(SURFACE) + WebGL
 * - H5：<canvas> + WebGL
 * - 小程序：<canvas type="webgl">
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
     * 设置画布清屏颜色（`#AARRGGBB` 十六进制字符串，如 `#FFFF0000` 为不透明红色）。
     * 不设置时各端统一使用红色，保证默认表现一致。
     */
    fun clearColor(color: String): CanvasGLViewAttr {
        CLEAR_COLOR with color
        return this
    }

    companion object {
        const val CLEAR_COLOR = "clearColor"
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
