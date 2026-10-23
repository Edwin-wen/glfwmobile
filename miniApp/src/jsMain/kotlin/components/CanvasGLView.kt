package components

import com.edwin.glfwmobile.canvas.WebSceneOp
import com.edwin.glfwmobile.canvas.parseWebScene
import com.edwin.glfwmobile.canvas.parseWebColor
import com.edwin.glfwmobile.canvas.toCssRgba
import com.tencent.kuikly.core.render.web.export.IKuiklyRenderViewExport
import com.tencent.kuikly.core.render.web.ktx.Frame
import com.tencent.kuikly.core.render.web.ktx.KRCssConst
import com.tencent.kuikly.core.render.web.ktx.KuiklyRenderCallback
import com.tencent.kuikly.core.render.web.runtime.miniapp.MiniGlobal
import com.tencent.kuikly.core.render.web.utils.Log
import dom.MiniCanvasGLViewElement
import org.w3c.dom.Element

/**
 * 小程序 CanvasGLView：复用运行时内置的 `canvas` 模板（`type="2d"`）。
 *
 * 绘制输入已统一为 scene ops（与原生 canvasCore / H5 CanvasKit 同一份 JSON），
 * 这里用 Canvas2D 作为播放器：小程序无 C++ 运行时，CanvasKit(WASM) 在包体与
 * WXWebAssembly 兼容性上风险较高，先用 Canvas2D 覆盖当前 op 集合。
 *
 * 节点异步渲染，首次查询可能为空，沿用有界重试 + 纯色兜底。
 */
class CanvasGLView : IKuiklyRenderViewExport {

    private val canvasElement = MiniCanvasGLViewElement()

    private var ctx: dynamic = null
    private var canvasNode: dynamic = null
    private var clearColor: String = DEFAULT_CLEAR_COLOR
    private var ops: List<WebSceneOp> = emptyList()
    private var readyCallback: KuiklyRenderCallback? = null
    private var readyNotified = false
    private var fallbackToSolidColor = false
    private var retryCount = 0
    private var canvasWidth = 0
    private var canvasHeight = 0
    private var pixelRatio = 0.0

    override val ele: Element
        get() = canvasElement.unsafeCast<Element>()

    init {
        // Canvas2D 播放器：显式声明 2d 类型，模板会透传到 <canvas type="{{i.p2}}">
        canvasElement.canvasType = MiniCanvasGLViewElement.TYPE_2D
        applyScene(clearScene(clearColor))
    }

    override fun setProp(propKey: String, propValue: Any): Boolean {
        return when (propKey) {
            CLEAR_COLOR -> {
                clearColor = propValue.unsafeCast<String>()
                if (parseWebColor(clearColor) != null) {
                    applyScene(clearScene(clearColor))
                }
                true
            }

            SCENE -> {
                applyScene(propValue.unsafeCast<String>())
                true
            }

            ON_READY -> {
                readyCallback = propValue.unsafeCast<KuiklyRenderCallback>()
                if (readyNotified) {
                    notifyReady()
                }
                true
            }

            KRCssConst.FRAME -> {
                val frame = propValue.unsafeCast<Frame>()
                canvasElement.setFrame(frame)
                canvasWidth = frame.width.toInt().coerceAtLeast(1)
                canvasHeight = frame.height.toInt().coerceAtLeast(1)
                render()
                onFrameChange(frame)
                true
            }

            else -> super.setProp(propKey, propValue)
        }
    }

    override fun resetProp(propKey: String): Boolean {
        return when (propKey) {
            CLEAR_COLOR, SCENE -> {
                clearColor = DEFAULT_CLEAR_COLOR
                applyScene(clearScene(clearColor))
                true
            }

            else -> super.resetProp(propKey)
        }
    }

    // ---------------- 内部实现 ----------------

    private fun applyScene(sceneJson: String) {
        val parsed = parseWebScene(sceneJson)
        if (parsed == null) {
            Log.warn(TAG, "invalid scene, keep current frame")
            return
        }
        ops = parsed
        render()
    }

    private fun render() {
        if (fallbackToSolidColor) {
            applyFallbackColor()
            return
        }
        if (ops.isEmpty() || canvasWidth <= 0 || canvasHeight <= 0) {
            return
        }
        if (ctx == null) {
            obtainContextAndRender()
            return
        }
        draw()
    }

    /** 小程序节点异步渲染，首次查询可能拿不到，用有界重试 */
    private fun obtainContextAndRender() {
        canvasElement.queryCanvasNode { node ->
            if (ctx != null) {
                return@queryCanvasNode
            }
            if (node == null) {
                if (retryCount < MAX_QUERY_RETRY) {
                    retryCount++
                    MiniGlobal.setTimeout({ obtainContextAndRender() }, RETRY_DELAY_MILLIS)
                } else {
                    Log.warn(TAG, "canvas node not found after retry, fallback to solid color")
                    applyFallbackColor()
                }
                return@queryCanvasNode
            }
            val context = node.getContext("2d")
            if (context == null) {
                Log.warn(TAG, "Canvas2D is unavailable, fallback to solid background")
                applyFallbackColor()
                return@queryCanvasNode
            }
            canvasNode = node
            ctx = context
            applyCanvasSize(node)
            draw()
        }
    }

    private fun applyCanvasSize(node: dynamic) {
        val ratio = dpr()
        node.width = (canvasWidth * ratio).toInt().coerceAtLeast(1)
        node.height = (canvasHeight * ratio).toInt().coerceAtLeast(1)
    }

    private fun draw() {
        val context = ctx ?: return
        val ratio = dpr()
        context.setTransform(ratio, 0.0, 0.0, ratio, 0.0, 0.0)
        context.clearRect(0.0, 0.0, canvasWidth.toDouble(), canvasHeight.toDouble())

        for (op in ops) {
            when (op) {
                is WebSceneOp.Clear -> {
                    context.fillStyle = op.color.toCssRgba()
                    context.fillRect(0.0, 0.0, canvasWidth.toDouble(), canvasHeight.toDouble())
                }

                is WebSceneOp.Rect -> {
                    context.fillStyle = op.color.toCssRgba()
                    context.fillRect(op.x, op.y, op.w, op.h)
                }

                is WebSceneOp.Line -> {
                    context.strokeStyle = op.color.toCssRgba()
                    context.lineWidth = op.width
                    context.lineCap = "round"
                    context.beginPath()
                    context.moveTo(op.x1, op.y1)
                    context.lineTo(op.x2, op.y2)
                    context.stroke()
                }
            }
        }
        notifyReady()
    }

    private fun applyFallbackColor() {
        fallbackToSolidColor = true
        val argb = parseWebColor(clearColor) ?: DEFAULT_CLEAR_COLOR_VALUE
        canvasElement.style.backgroundColor = argb.toCssRgba()
        notifyReady()
    }

    private fun notifyReady() {
        if (readyNotified) {
            return
        }
        readyNotified = true
        readyCallback?.invoke(mapOf("viewName" to VIEW_NAME))
    }

    private fun dpr(): Double {
        if (pixelRatio <= 0.0) {
            pixelRatio = try {
                val info: dynamic = js("wx.getWindowInfo ? wx.getWindowInfo() : wx.getSystemInfoSync()")
                (info.pixelRatio as Number).toDouble()
            } catch (e: dynamic) {
                DEFAULT_PIXEL_RATIO
            }
        }
        return pixelRatio
    }

    private fun clearScene(color: String): String =
        """{"ops":[{"op":"clear","color":"$color"}]}"""

    companion object {
        const val VIEW_NAME = "CanvasGLView"
        private const val TAG = "CanvasGLView"
        private const val CLEAR_COLOR = "clearColor"
        private const val SCENE = "scene"
        private const val ON_READY = "onReady"
        private const val DEFAULT_CLEAR_COLOR = "#FFFF0000"
        private const val DEFAULT_CLEAR_COLOR_VALUE = 0xFFFF0000L
        private const val DEFAULT_PIXEL_RATIO = 2.0
        private const val MAX_QUERY_RETRY = 20
        private const val RETRY_DELAY_MILLIS = 50
    }
}