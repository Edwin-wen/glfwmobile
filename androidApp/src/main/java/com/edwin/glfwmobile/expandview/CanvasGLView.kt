package com.edwin.glfwmobile.expandview

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.util.Log
import android.view.View
import com.tencent.kuikly.core.render.android.export.IKuiklyRenderViewExport
import com.tencent.kuikly.core.render.android.export.KuiklyRenderCallback

/**
 * Kuikly 自定义 View：Skia 画布（Android 宿主）。
 *
 * 本类只负责三件事：
 * 1. 把 Kuikly 属性翻译成 [skc] 调用；
 * 2. 管理一块 RGBA Bitmap 内存；
 * 3. 把画好的 Bitmap 提交给系统渲染。
 *
 * 真正的绘制逻辑在 `canvasCore/`（C++ / Skia），与 iOS、鸿蒙共用同一份实现；
 * 组件注册名 [VIEW_NAME] 必须与 Kuikly 侧 `CanvasGLView.viewName()` 一致。
 */
class CanvasGLView(context: Context) : View(context), IKuiklyRenderViewExport {

    private var handle: Long = nativeCreate()

    /** 只有尺寸确定后才分配；与 Skia 的 RGBA_8888 目标内存一一对应 */
    private var bitmap: Bitmap? = null

    private var clearColor: String = DEFAULT_CLEAR_COLOR
    private var onReadyCallback: KuiklyRenderCallback? = null
    private var firstFrameDrawn = false
    private var readyEventFired = false

    init {
        setWillNotDraw(false)
    }

    // ---------------- IKuiklyRenderViewExport ----------------

    override fun setProp(propKey: String, propValue: Any): Boolean {
        return when (propKey) {
            CLEAR_COLOR_PROP -> {
                applyClearColor(propValue.toString())
                true
            }

            SCENE_PROP -> {
                applyScene(propValue.toString())
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
                applyClearColor(DEFAULT_CLEAR_COLOR)
                true
            }

            SCENE_PROP -> {
                applyClearColor(clearColor)
                true
            }

            else -> super.resetProp(propKey)
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w <= 0 || h <= 0) {
            return
        }
        bitmap?.recycle()
        bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        // Kuikly 的尺寸单位是 dp，scene 坐标同样以 dp 表达，交给 canvas_core 统一缩放
        nativeSetSize(handle, w, h, resources.displayMetrics.density)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val target = bitmap ?: return
        if (handle == 0L) {
            return
        }
        if (!renderInto(target)) {
            return
        }
        canvas.drawBitmap(target, 0f, 0f, null)
        firstFrameDrawn = true
        fireReadyIfNeeded()
    }

    override fun onDestroy() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
        bitmap?.recycle()
        bitmap = null
        onReadyCallback = null
    }

    // ---------------- 内部实现 ----------------

    private fun applyClearColor(color: String) {
        val argb = parseArgb(color) ?: run {
            Log.w(TAG, "invalid clearColor: $color, keep current color")
            return
        }
        clearColor = color
        nativeSetClearColor(handle, argb)
        invalidate()
    }

    /**
     * scene 是跨端统一的绘制指令（JSON），由 `canvasCore` 解析。
     * 解析失败时 canvas_core 保留上一帧，这里只记录日志，不打断渲染。
     */
    private fun applyScene(json: String) {
        nativeSetScene(handle, json)
        val error = nativeLastError(handle)
        if (error.isNotEmpty()) {
            Log.w(TAG, "invalid scene: $error")
            return
        }
        invalidate()
    }

    private fun renderInto(target: Bitmap): Boolean {
        val ok = nativeRenderBitmap(handle, target)
        if (!ok) {
            Log.w(TAG, "render failed: ${nativeLastError(handle)}")
        }
        return ok
    }

    private fun fireReadyIfNeeded() {
        if (!firstFrameDrawn || readyEventFired) {
            return
        }
        val callback = onReadyCallback ?: return
        readyEventFired = true
        callback.invoke(mapOf("viewName" to VIEW_NAME))
    }

    /** 解析 `#AARRGGBB` / `#RRGGBB` / `#RGB`，失败返回 null（保留原颜色） */
    private fun parseArgb(colorString: String): Int? {
        val hex = colorString.trim().removePrefix("#")
        val normalized = when (hex.length) {
            3 -> "FF" + hex.map { "$it$it" }.joinToString("")
            6 -> "FF$hex"
            8 -> hex
            else -> return null
        }
        return normalized.toLongOrNull(16)?.toInt()
    }

    // ---------------- JNI：canvas_core 的唯一入口 ----------------

    private external fun nativeCreate(): Long

    private external fun nativeDestroy(handle: Long)

    private external fun nativeSetSize(handle: Long, width: Int, height: Int, dpr: Float)

    private external fun nativeSetClearColor(handle: Long, argb: Int)

    private external fun nativeSetScene(handle: Long, sceneJson: String)

    private external fun nativeRenderBitmap(handle: Long, bitmap: Bitmap): Boolean

    private external fun nativeLastError(handle: Long): String

    companion object {
        /** 组件注册名，必须与 Kuikly 侧 viewName 一致 */
        const val VIEW_NAME = "CanvasGLView"

        private const val TAG = "CanvasGLView"
        private const val CLEAR_COLOR_PROP = "clearColor"
        private const val SCENE_PROP = "scene"
        private const val ON_READY_PROP = "onReady"

        /** 默认不透明红色 */
        private const val DEFAULT_CLEAR_COLOR = "#FFFF0000"

        init {
            System.loadLibrary("skia_canvas")
        }
    }
}