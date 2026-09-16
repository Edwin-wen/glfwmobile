package components

import com.tencent.kuikly.core.render.web.export.IKuiklyRenderViewExport
import com.tencent.kuikly.core.render.web.ktx.Frame
import com.tencent.kuikly.core.render.web.ktx.KRCssConst
import com.tencent.kuikly.core.render.web.ktx.KuiklyRenderCallback
import com.tencent.kuikly.core.render.web.runtime.miniapp.MiniGlobal
import com.tencent.kuikly.core.render.web.utils.Log
import dom.MiniCanvasGLViewElement
import org.w3c.dom.Element

/**
 * Mini program CanvasGLView, rendering a solid color (red by default) with a WebGL canvas.
 *
 * The view name must be the same as the Kuikly side
 * `com.edwin.glfwmobile.canvas.CanvasGLView`.
 *
 * The mini program node is rendered asynchronously, so the canvas node is obtained
 * with a retrying SelectorQuery node query. When WebGL is unavailable, it degrades
 * to a solid CSS background color to keep the page from turning blank.
 */
class CanvasGLView : IKuiklyRenderViewExport {

    private val canvasElement = MiniCanvasGLViewElement()

    private var gl: dynamic = null
    private var canvasNode: dynamic = null
    private var clearColor: String = DEFAULT_CLEAR_COLOR
    private var readyCallback: KuiklyRenderCallback? = null
    private var readyNotified = false
    private var fallbackToSolidColor = false
    private var retryCount = 0
    private var canvasWidth = 0
    private var canvasHeight = 0

    override val ele: Element
        get() = canvasElement.unsafeCast<Element>()

    init {
        // A WebGL canvas must declare its type explicitly
        canvasElement.canvasType = MiniCanvasGLViewElement.TYPE_WEBGL
    }

    override fun setProp(propKey: String, propValue: Any): Boolean {
        return when (propKey) {
            CLEAR_COLOR -> {
                clearColor = propValue.unsafeCast<String>()
                render()
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

    /**
     * Clear the canvas once with the current color
     */
    private fun render() {
        val context = gl
        if (context == null) {
            if (fallbackToSolidColor) {
                applyFallbackColor()
            } else {
                obtainContextAndRender()
            }
            return
        }
        val node = canvasNode
        if (node != null) {
            // WebGL canvas needs an explicit drawing buffer size
            if (node.width != canvasWidth) {
                node.width = canvasWidth
            }
            if (node.height != canvasHeight) {
                node.height = canvasHeight
            }
        }
        val color = parseColor(clearColor)
        context.viewport(0, 0, canvasWidth, canvasHeight)
        context.clearColor(color[0], color[1], color[2], color[3])
        context.clear(COLOR_BUFFER_BIT)
        notifyReady()
    }

    /**
     * The canvas node is only available after the page data is flushed,
     * so query it with a bounded retry
     */
    private fun obtainContextAndRender() {
        canvasElement.queryCanvasNode { node ->
            if (gl != null) {
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
            canvasNode = node
            val context = node.getContext("webgl")
            if (context == null) {
                Log.warn(TAG, "WebGL is unavailable, fallback to solid background: $clearColor")
                applyFallbackColor()
                return@queryCanvasNode
            }
            gl = context
            render()
        }
    }

    /**
     * Degrade to a solid CSS background color when WebGL is unavailable
     */
    private fun applyFallbackColor() {
        fallbackToSolidColor = true
        canvasElement.style.backgroundColor = toCssColor(parseColor(clearColor))
        notifyReady()
    }

    private fun notifyReady() {
        if (readyNotified) {
            return
        }
        readyNotified = true
        readyCallback?.invoke(null)
    }

    /**
     * Parse `#AARRGGBB` / `#RRGGBB` / `#RGB` color into [r, g, b, a] (0f ~ 1f)
     */
    private fun parseColor(color: String): FloatArray {
        val hex = color.removePrefix("#")
        val normalized = when (hex.length) {
            8 -> hex
            6 -> "FF$hex"
            3 -> "FF" + hex.map { "$it$it" }.joinToString("")
            else -> DEFAULT_CLEAR_COLOR.removePrefix("#")
        }
        val value = normalized.toLongOrNull(16) ?: return floatArrayOf(1f, 0f, 0f, 1f)
        val a = ((value shr 24) and 0xFF) / 255f
        val r = ((value shr 16) and 0xFF) / 255f
        val g = ((value shr 8) and 0xFF) / 255f
        val b = (value and 0xFF) / 255f
        return floatArrayOf(r, g, b, a)
    }

    private fun toCssColor(color: FloatArray): String {
        val r = (color[0] * 255).toInt()
        val g = (color[1] * 255).toInt()
        val b = (color[2] * 255).toInt()
        return "rgba($r, $g, $b, ${color[3]})"
    }

    companion object {
        const val VIEW_NAME = "CanvasGLView"
        private const val TAG = "CanvasGLView"
        private const val CLEAR_COLOR = "clearColor"
        private const val ON_READY = "onReady"
        private const val DEFAULT_CLEAR_COLOR = "#FFFF0000"
        private const val MAX_QUERY_RETRY = 20
        private const val RETRY_DELAY_MILLIS = 50

        /** WebGLRenderingContext.COLOR_BUFFER_BIT */
        private const val COLOR_BUFFER_BIT = 0x4000
    }
}
