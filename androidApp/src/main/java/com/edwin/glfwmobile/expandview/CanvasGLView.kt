package com.edwin.glfwmobile.expandview

import android.content.Context
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import com.tencent.kuikly.core.render.android.export.IKuiklyRenderViewExport
import com.tencent.kuikly.core.render.android.export.KuiklyRenderCallback
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * Kuikly 自定义 View：原生 OpenGL ES 画布（Android 实现）。
 *
 * 对应 Kuikly 侧 `com.edwin.glfwmobile.canvas.CanvasGLView`（`viewName = "CanvasGLView"`），
 * 注册名为 [VIEW_NAME]，三处必须保持一致。
 *
 * 说明：GLSurfaceView 使用独立 Surface 渲染，不支持圆角与半透明叠加，适合整页铺满的场景。
 * 由于画面是静态的，使用 [RENDERMODE_WHEN_DIRTY] 按需渲染（仅在尺寸变化/属性变化时重绘），
 * 避免默认 60fps 的无意义重绘带来的功耗。
 */
class CanvasGLView(context: Context) : GLSurfaceView(context), IKuiklyRenderViewExport {

    /** 清屏颜色（RGBA 各分量 0~1），默认不透明红色 */
    @Volatile
    private var clearColor: FloatArray = DEFAULT_CLEAR_COLOR

    private var onReadyCallback: KuiklyRenderCallback? = null

    /** 是否已完成首帧绘制 */
    @Volatile
    private var firstFrameDrawn = false

    /** onReady 事件是否已派发，避免重复回调 */
    private var readyEventFired = false

    private val glRenderer = object : Renderer {
        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            applyClearColor()
            // 首帧由 setRenderer 触发的初始绘制发起，此处仅确保上下文创建后有一帧输出
            requestRender()
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            GLES20.glViewport(0, 0, width, height)
            requestRender()
        }

        override fun onDrawFrame(gl: GL10?) {
            applyClearColor()
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
            firstFrameDrawn = true
            fireReadyIfNeeded()
        }
    }

    init {
        setEGLContextClientVersion(2)
        setRenderer(glRenderer)
        renderMode = RENDERMODE_WHEN_DIRTY
    }

    // ---------------- IKuiklyRenderViewExport ----------------

    override fun setProp(propKey: String, propValue: Any): Boolean {
        return when (propKey) {
            CLEAR_COLOR_PROP -> {
                parseClearColor(propValue.toString())?.also {
                    clearColor = it
                    requestRender()
                }
                true
            }

            ON_READY_PROP -> {
                onReadyCallback = propValue as KuiklyRenderCallback
                fireReadyIfNeeded()
                true
            }

            else -> super.setProp(propKey, propValue)
        }
    }

    override fun resetProp(propKey: String): Boolean {
        return when (propKey) {
            CLEAR_COLOR_PROP -> {
                clearColor = DEFAULT_CLEAR_COLOR
                requestRender()
                true
            }

            else -> super.resetProp(propKey)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        onReadyCallback = null
    }

    // ---------------- 内部实现 ----------------

    /** 必须在 GL 线程调用 */
    private fun applyClearColor() {
        val color = clearColor
        GLES20.glClearColor(color[0], color[1], color[2], color[3])
    }

    private fun fireReadyIfNeeded() {
        if (!firstFrameDrawn || readyEventFired) {
            return
        }
        val callback = onReadyCallback ?: return
        readyEventFired = true
        // GL 线程回调统一切回主线程，保证 Kuikly 侧事件回调线程一致
        post {
            callback.invoke(mapOf("viewName" to VIEW_NAME))
        }
    }

    /**
     * 解析 `#AARRGGBB` / `#RRGGBB` 形式的颜色，解析失败返回 null（保留原颜色）
     */
    private fun parseClearColor(colorString: String): FloatArray? {
        val hex = colorString.trim().removePrefix("#")
        val value = hex.toLongOrNull(16) ?: return null
        return when (hex.length) {
            8 -> floatArrayOf(
                ((value shr 16) and 0xFF) / 255f,
                ((value shr 8) and 0xFF) / 255f,
                (value and 0xFF) / 255f,
                ((value shr 24) and 0xFF) / 255f
            )

            6 -> floatArrayOf(
                ((value shr 16) and 0xFF) / 255f,
                ((value shr 8) and 0xFF) / 255f,
                (value and 0xFF) / 255f,
                1f
            )

            else -> null
        }
    }

    companion object {
        /** 组件注册名，必须与 Kuikly 侧 viewName 一致 */
        const val VIEW_NAME = "CanvasGLView"

        private const val CLEAR_COLOR_PROP = "clearColor"
        private const val ON_READY_PROP = "onReady"

        /** 默认不透明红色 */
        private val DEFAULT_CLEAR_COLOR = floatArrayOf(1f, 0f, 0f, 1f)
    }
}
