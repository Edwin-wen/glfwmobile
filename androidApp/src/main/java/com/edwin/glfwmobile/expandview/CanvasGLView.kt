package com.edwin.glfwmobile.expandview

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.opengl.GLSurfaceView
import android.util.Log
import android.view.View
import android.widget.FrameLayout
import com.tencent.kuikly.core.render.android.export.IKuiklyRenderViewExport
import com.tencent.kuikly.core.render.android.export.KuiklyRenderCallback
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * Kuikly 自定义 View：Skia 画布（Android 宿主）。
 *
 * 容器本身不绘制，按 `backend` 属性挂载两种渲染子视图：
 * - raster：[RasterChild]（Bitmap + skc_render_raster）
 * - gpu   ：[GpuChild]（GLSurfaceView + skc_attach_gl/skc_render_gl）
 *
 * GPU 初始化失败时自动回退 raster，业务无感。绘制逻辑全部在 `cppCore/canvasCore`。
 */
class CanvasGLView(context: Context) : FrameLayout(context), IKuiklyRenderViewExport {

    private var sceneJson: String = DEFAULT_SCENE
    private var clearColor: String = DEFAULT_CLEAR_COLOR
    private var backend: String = BACKEND_AUTO
    private var forcedRaster = false

    private var child: CanvasChild? = null
    private var childKind: String? = null

    private var widthPx = 0
    private var heightPx = 0

    private var onReadyCallback: KuiklyRenderCallback? = null
    private var firstFrameDrawn = false
    private var readyEventFired = false

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

            BACKEND_PROP -> {
                backend = propValue.toString()
                forcedRaster = false
                ensureChild()
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

            BACKEND_PROP -> {
                backend = BACKEND_AUTO
                forcedRaster = false
                ensureChild()
                true
            }

            else -> super.resetProp(propKey)
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        widthPx = w
        heightPx = h
        // onSizeChanged 处于 layout 期间，此时 addView 的 requestLayout 会被忽略，
        // 推迟到下一帧再挂载子视图
        post {
            ensureChild()
            child?.setSize(w, h, resources.displayMetrics.density)
        }
    }

    override fun onDestroy() {
        removeChild()
        onReadyCallback = null
    }

    // ---------------- 子视图管理 ----------------

    private fun ensureChild() {
        if (widthPx <= 0 || heightPx <= 0) {
            return
        }
        val desired = when {
            forcedRaster -> KIND_RASTER
            backend == BACKEND_RASTER -> KIND_RASTER
            else -> KIND_GPU // gpu / auto：先尝试 GPU，失败回退
        }
        if (childKind == desired && child != null) {
            return
        }
        removeChild()
        val newChild: CanvasChild = if (desired == KIND_GPU) {
            GpuChild(
                context = context,
                density = resources.displayMetrics.density,
                onFrameDrawn = { notifyFrameDrawn() },
                onUnavailable = { onGpuUnavailable() }
            )
        } else {
            RasterChild(context = context, onFrameDrawn = { notifyFrameDrawn() })
        }
        child = newChild
        childKind = desired
        Log.i(TAG, "render backend=$desired")
        addView(newChild.view, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        // 兜底：确保子视图立刻拿到真实尺寸（GLSurfaceView 需要非零尺寸才会创建 surface）
        newChild.view.layout(0, 0, widthPx, heightPx)
        newChild.setSize(widthPx, heightPx, resources.displayMetrics.density)
        newChild.setScene(sceneJson)
        newChild.setClearColor(clearColor)
    }

    private fun removeChild() {
        child?.let {
            removeView(it.view)
            it.destroy()
        }
        child = null
        childKind = null
    }

    private fun onGpuUnavailable() {
        post {
            if (forcedRaster) {
                return@post
            }
            Log.w(TAG, "GPU backend unavailable, fallback to raster")
            forcedRaster = true
            ensureChild()
        }
    }

    // ---------------- 属性转发 ----------------

    private fun applyClearColor(color: String) {
        if (parseArgb(color) == null) {
            Log.w(TAG, "invalid clearColor: $color, keep current color")
            return
        }
        clearColor = color
        child?.setClearColor(color)
    }

    private fun applyScene(json: String) {
        sceneJson = json
        child?.setScene(json)
    }

    private fun notifyFrameDrawn() {
        if (!firstFrameDrawn) {
            firstFrameDrawn = true
        }
        post { fireReadyIfNeeded() }
    }

    private fun fireReadyIfNeeded() {
        if (!firstFrameDrawn || readyEventFired) {
            return
        }
        val callback = onReadyCallback ?: return
        readyEventFired = true
        callback.invoke(mapOf("viewName" to VIEW_NAME))
    }

    companion object {
        /** 组件注册名，必须与 Kuikly 侧 viewName 一致 */
        const val VIEW_NAME = "CanvasGLView"

        private const val TAG = "CanvasGLView"
        private const val CLEAR_COLOR_PROP = "clearColor"
        private const val SCENE_PROP = "scene"
        private const val BACKEND_PROP = "backend"
        private const val ON_READY_PROP = "onReady"

        private const val BACKEND_RASTER = "raster"
        private const val BACKEND_AUTO = "auto"
        private const val KIND_RASTER = "raster"
        private const val KIND_GPU = "gpu"

        /** 默认不透明红色 */
        private const val DEFAULT_CLEAR_COLOR = "#FFFF0000"
        private const val DEFAULT_SCENE = """{"ops":[{"op":"clear","color":"#FFFF0000"}]}"""
    }
}

// ---------------- 子视图 ----------------

private interface CanvasChild {
    val view: View
    fun setScene(sceneJson: String)
    fun setClearColor(color: String)
    fun setSize(widthPx: Int, heightPx: Int, density: Float)
    fun destroy()
}

/** CPU 光栅化：Skia 画进 Bitmap，再由 View 提交 */
private class RasterChild(
    context: Context,
    private val onFrameDrawn: () -> Unit
) : View(context), CanvasChild {

    private var handle: Long = CanvasNative.nativeCreate()
    private var bitmap: Bitmap? = null

    init {
        setWillNotDraw(false)
    }

    override val view: View
        get() = this

    override fun setScene(sceneJson: String) {
        CanvasNative.nativeSetScene(handle, sceneJson)
        invalidate()
    }

    override fun setClearColor(color: String) {
        val argb = parseArgb(color) ?: return
        CanvasNative.nativeSetClearColor(handle, argb)
        invalidate()
    }

    override fun setSize(widthPx: Int, heightPx: Int, density: Float) {
        if (widthPx <= 0 || heightPx <= 0) {
            return
        }
        bitmap?.recycle()
        bitmap = Bitmap.createBitmap(widthPx, heightPx, Bitmap.Config.ARGB_8888)
        CanvasNative.nativeSetSize(handle, widthPx, heightPx, density)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val target = bitmap ?: return
        if (handle == 0L) {
            return
        }
        if (!CanvasNative.nativeRenderBitmap(handle, target)) {
            Log.w("CanvasGLView", "raster render failed: ${CanvasNative.nativeLastError(handle)}")
            return
        }
        canvas.drawBitmap(target, 0f, 0f, null)
        onFrameDrawn()
    }

    override fun destroy() {
        if (handle != 0L) {
            CanvasNative.nativeDestroy(handle)
            handle = 0L
        }
        bitmap?.recycle()
        bitmap = null
    }
}

/** GPU：GLSurfaceView 提供 EGL 上下文，Skia Ganesh 直接画到默认帧缓冲 */
private class GpuChild(
    context: Context,
    private val density: Float,
    private val onFrameDrawn: () -> Unit,
    private val onUnavailable: () -> Unit
) : GLSurfaceView(context), GLSurfaceView.Renderer, CanvasChild {

    @Volatile
    private var handle: Long = CanvasNative.nativeCreate()

    @Volatile
    private var attached = false

    @Volatile
    private var sceneJson: String? = null

    @Volatile
    private var clearColor: String? = null

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 8, 0, 8)
        setRenderer(this)
        renderMode = RENDERMODE_WHEN_DIRTY
        preserveEGLContextOnPause = true
    }

    override val view: View
        get() = this

    override fun setScene(sceneJson: String) {
        this.sceneJson = sceneJson
        queueGl { CanvasNative.nativeSetScene(handle, sceneJson) }
    }

    override fun setClearColor(color: String) {
        this.clearColor = color
        val argb = parseArgb(color) ?: return
        queueGl { CanvasNative.nativeSetClearColor(handle, argb) }
    }

    override fun setSize(widthPx: Int, heightPx: Int, density: Float) {
        // 尺寸由 onSurfaceChanged 回传，这里只保证密度生效
    }

    override fun destroy() {
        if (handle == 0L) {
            return
        }
        if (attached) {
            // GrDirectContext 的析构需要 GL 上下文，必须回到 GL 线程
            try {
                queueEvent {
                    val h = handle
                    handle = 0L
                    if (h != 0L) {
                        CanvasNative.nativeDetachGpu(h)
                        CanvasNative.nativeDestroy(h)
                    }
                }
                return
            } catch (e: IllegalStateException) {
                Log.w("CanvasGLView", "GL thread already stopped, skip detach")
            }
        }
        CanvasNative.nativeDestroy(handle)
        handle = 0L
    }

    // ---------------- Renderer ----------------

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        attached = CanvasNative.nativeAttachGl(handle)
        if (!attached) {
            Log.w("CanvasGLView", "attach GL failed: ${CanvasNative.nativeLastError(handle)}")
            onUnavailable()
            return
        }
        sceneJson?.let { CanvasNative.nativeSetScene(handle, it) }
        clearColor?.let { parseArgb(it)?.let { argb -> CanvasNative.nativeSetClearColor(handle, argb) } }
        requestRender()
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        CanvasNative.nativeSetSize(handle, width, height, density)
        if (attached) {
            requestRender()
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        if (!attached) {
            return
        }
        if (CanvasNative.nativeRenderGl(handle, 0, width, height)) {
            onFrameDrawn()
        } else {
            Log.w("CanvasGLView", "gpu render failed: ${CanvasNative.nativeLastError(handle)}")
        }
    }

    private fun queueGl(block: () -> Unit) {
        try {
            queueEvent {
                if (attached) {
                    block()
                    requestRender()
                }
            }
        } catch (e: IllegalStateException) {
            // 视图尚未进入窗口，属性会在 onSurfaceCreated 里统一补齐
        }
    }
}

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