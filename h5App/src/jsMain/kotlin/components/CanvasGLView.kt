package components

import com.tencent.kuikly.core.render.web.export.IKuiklyRenderViewExport
import com.tencent.kuikly.core.render.web.ktx.Frame
import com.tencent.kuikly.core.render.web.ktx.KRCssConst
import com.tencent.kuikly.core.render.web.ktx.KuiklyRenderCallback
import com.tencent.kuikly.core.render.web.ktx.setFrame
import com.tencent.kuikly.core.render.web.utils.Log
import kotlinx.browser.document
import org.w3c.dom.HTMLCanvasElement

/**
 * H5 端 CanvasGLView 实现：使用 `<canvas>` + WebGL 清屏为纯色（默认红色）。
 *
 * 与 Kuikly 侧 `com.edwin.glfwmobile.canvas.CanvasGLView` 通过 viewName 关联，
 * 组件名必须保持完全一致。
 *
 * 静态画面只在尺寸/颜色变化时重绘一次，不跑常驻渲染循环。
 */
class CanvasGLView : IKuiklyRenderViewExport {

    private val canvas = document.createElement("canvas").unsafeCast<HTMLCanvasElement>()

    private var gl: dynamic = null
    private var clearColor: String = DEFAULT_CLEAR_COLOR
    private var readyCallback: KuiklyRenderCallback? = null
    private var readyNotified = false

    override val ele: HTMLCanvasElement
        get() = canvas

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
                ele.setFrame(frame, ele.style)
                // canvas 的绘制缓冲区尺寸需要与布局尺寸保持一致，否则 WebGL 画面会被拉伸
                ele.width = frame.width.toInt().coerceAtLeast(1)
                ele.height = frame.height.toInt().coerceAtLeast(1)
                render()
                onFrameChange(frame)
                true
            }

            else -> super.setProp(propKey, propValue)
        }
    }

    /**
     * 获取 WebGL 上下文，失败返回 null
     */
    private fun obtainContext(): dynamic {
        if (gl == null) {
            val dynamicCanvas = ele.asDynamic()
            gl = dynamicCanvas.getContext("webgl") ?: dynamicCanvas.getContext("experimental-webgl")
        }
        return gl
    }

    /**
     * 用当前颜色清屏一次
     */
    private fun render() {
        val context = obtainContext()
        val color = parseColor(clearColor)
        if (context == null) {
            // 降级：WebGL 不可用时退化为纯色背景，保证页面不白屏
            Log.warn(TAG, "WebGL is unavailable, fallback to solid background: $clearColor")
            ele.style.backgroundColor = toCssColor(color)
            notifyReady()
            return
        }
        context.viewport(0, 0, ele.width, ele.height)
        context.clearColor(color[0], color[1], color[2], color[3])
        context.clear(COLOR_BUFFER_BIT)
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
     * 解析 `#AARRGGBB` / `#RRGGBB` / `#RGB` 颜色，返回 [r, g, b, a]（0f ~ 1f）
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

        /** WebGLRenderingContext.COLOR_BUFFER_BIT */
        private const val COLOR_BUFFER_BIT = 0x4000
    }
}
