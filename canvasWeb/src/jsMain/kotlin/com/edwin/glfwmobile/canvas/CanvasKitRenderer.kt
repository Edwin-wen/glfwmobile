package com.edwin.glfwmobile.canvas

import org.w3c.dom.HTMLCanvasElement

/**
 * H5 端 Skia 播放器：CanvasKit（Skia 的 WASM 构建）。
 *
 * 与原生端 `cppCore/canvasCore/src/painter.cpp` 逐条 op 对齐：
 * clear -> SkCanvas::clear，rect -> drawRect，line -> drawLine。
 *
 * 降级链：WebGL surface -> 软件 surface -> CSS 纯色（不影响页面可视结果）。
 */
public class CanvasKitRenderer(
    private val canvas: HTMLCanvasElement,
    private val onReady: () -> Unit,
    private val onUnavailable: (String) -> Unit,
) {

    private var kit: dynamic = null
    private var surface: dynamic = null
    private var paint: dynamic = null

    private var ops: List<WebSceneOp> = emptyList()
    private var fallbackColor: Long = DEFAULT_CLEAR_COLOR
    private var backend: String = "auto"
    private var width: Int = 0
    private var height: Int = 0
    private var surfaceWidth: Int = 0
    private var surfaceHeight: Int = 0

    private var loading = false
    private var unavailable = false
    private var readyFired = false
    private var firstFrameDrawn = false

    fun setScene(sceneJson: String?) {
        val parsed = sceneJson?.let { parseWebScene(it) }
        if (parsed != null) {
            ops = parsed
            parsed.firstClearColor()?.let { fallbackColor = it }
        }
        render()
    }

    fun setFallbackColor(color: Long) {
        fallbackColor = color
        render()
    }

    /** 后端：raster 走 CPU surface，gpu/auto 优先 WebGL */
    fun setBackend(backend: String) {
        if (backend == this.backend) {
            return
        }
        this.backend = backend
        surface?.delete()
        surface = null
        surfaceWidth = 0
        surfaceHeight = 0
        render()
    }

    /** 逻辑尺寸（CSS px），物理像素由 devicePixelRatio 换算 */
    fun resize(width: Int, height: Int) {
        if (width == this.width && height == this.height) {
            return
        }
        this.width = width
        this.height = height
        render()
    }

    fun dispose() {
        surface?.delete()
        surface = null
        paint?.delete()
        paint = null
        kit = null
    }

    // ---------------- 内部实现 ----------------

    private fun render() {
        if (unavailable) {
            applyCssFallback()
            return
        }
        if (ops.isEmpty() || width <= 0 || height <= 0) {
            return
        }
        val instance = kit
        if (instance == null) {
            ensureKit()
            return
        }
        if (!ensureSurface(instance)) {
            return
        }

        val skCanvas = surface.getCanvas()
        val scale = dpr()
        skCanvas.save()
        skCanvas.scale(scale, scale)
        for (op in ops) {
            when (op) {
                is WebSceneOp.Clear -> skCanvas.clear(color(instance, op.color))
                is WebSceneOp.Rect -> {
                    paint.setStyle(instance.PaintStyle.Fill)
                    paint.setColor(color(instance, op.color))
                    skCanvas.drawRect(instance.XYWHRect(op.x, op.y, op.w, op.h), paint)
                }

                is WebSceneOp.Line -> {
                    paint.setStyle(instance.PaintStyle.Stroke)
                    paint.setStrokeWidth(op.width)
                    paint.setStrokeCap(instance.StrokeCap.Round)
                    paint.setColor(color(instance, op.color))
                    skCanvas.drawLine(op.x1, op.y1, op.x2, op.y2, paint)
                }
            }
        }
        skCanvas.restore()
        surface.flush()
        firstFrameDrawn = true
        fireReady()
    }

    private fun ensureKit() {
        if (loading) {
            return
        }
        if (js("typeof CanvasKitInit") != "function") {
            markUnavailable("CanvasKitInit is not loaded")
            return
        }
        loading = true
        initCanvasKit(
            locateFileOptions,
            { instance ->
                kit = instance
                paint = newPaint(instance)
                paint.setAntiAlias(true)
                loading = false
                render()
            },
            { error ->
                loading = false
                markUnavailable("CanvasKitInit failed: $error")
            }
        )
    }

    private fun ensureSurface(instance: dynamic): Boolean {
        val pixelWidth = (width * dpr()).toInt().coerceAtLeast(1)
        val pixelHeight = (height * dpr()).toInt().coerceAtLeast(1)
        if (surface != null && surfaceWidth == pixelWidth && surfaceHeight == pixelHeight) {
            return true
        }
        surface?.delete()
        surface = null
        canvas.width = pixelWidth
        canvas.height = pixelHeight
        surface = try {
            when (backend) {
                "raster" -> instance.MakeSWCanvasSurface(canvas)
                else -> instance.MakeWebGLCanvasSurface(canvas) ?: instance.MakeSWCanvasSurface(canvas)
            }
        } catch (e: dynamic) {
            null
        }
        if (surface == null) {
            markUnavailable("CanvasKit surface unavailable")
            return false
        }
        surfaceWidth = pixelWidth
        surfaceHeight = pixelHeight
        return true
    }

    private fun markUnavailable(reason: String) {
        if (unavailable) {
            return
        }
        unavailable = true
        warn("$reason, fallback to solid background")
        onUnavailable(reason)
        applyCssFallback()
    }

    private fun warn(message: String) {
        warnJs(message)
    }

    private fun applyCssFallback() {
        canvas.style.backgroundColor = fallbackColor.toCssRgba()
        // 兜底也视为「已呈现一帧」，保证业务侧 onReady 语义一致
        firstFrameDrawn = true
        fireReady()
    }

    private fun fireReady() {
        if (!firstFrameDrawn || readyFired) {
            return
        }
        readyFired = true
        onReady()
    }

    private fun color(instance: dynamic, argb: Long): dynamic {
        val rgba = argb.toRgba()
        return instance.Color4f(rgba[0], rgba[1], rgba[2], rgba[3])
    }

    private fun dpr(): Float = (js("window.devicePixelRatio") as Number).toFloat()

    /*
     * Kotlin/JS 的 js() 代码会原样内联，引用 Kotlin 局部变量名不可靠（会被改名），
     * 因此跨语言逻辑固定成 JS 函数值，只通过参数交互。
     */
    private val locateFileOptions: dynamic =
        js("({ locateFile: function (file) { return './' + file; } })")

    private val initCanvasKit: dynamic =
        js("function (options, onLoaded, onFailed) { CanvasKitInit(options).then(function (ck) { onLoaded(ck); }, function (e) { onFailed(e); }); }")

    private val newPaint: dynamic = js("function (ck) { return new ck.Paint(); }")

    private val warnJs: dynamic = js("function (message) { console.warn(message); }")

    companion object {
        private const val TAG = "CanvasGLView"
        private const val DEFAULT_CLEAR_COLOR = 0xFFFF0000L
    }
}