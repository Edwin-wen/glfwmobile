package components

import com.edwin.glfwmobile.canvas.CanvasKitRenderer
import com.edwin.glfwmobile.canvas.parseWebColor
import com.tencent.kuikly.core.render.web.export.IKuiklyRenderViewExport
import com.tencent.kuikly.core.render.web.ktx.Frame
import com.tencent.kuikly.core.render.web.ktx.KRCssConst
import com.tencent.kuikly.core.render.web.ktx.KuiklyRenderCallback
import com.tencent.kuikly.core.render.web.ktx.setFrame
import kotlinx.browser.document
import org.w3c.dom.HTMLCanvasElement

/**
 * H5 端 CanvasGLView：`<canvas>` + CanvasKit（Skia 的 WASM 构建）。
 *
 * 与 Kuikly 侧 `com.edwin.glfwmobile.canvas.CanvasGLView` 通过 viewName 关联，
 * 组件名必须保持完全一致。绘制逻辑已统一到 scene ops，本类只做属性转发与尺寸同步。
 */
class CanvasGLView : IKuiklyRenderViewExport {

    private val canvas = document.createElement("canvas").unsafeCast<HTMLCanvasElement>()

    private var clearColor: String = DEFAULT_CLEAR_COLOR
    private var readyCallback: KuiklyRenderCallback? = null
    private var firstFrameDrawn = false
    private var readyNotified = false

    private val renderer = CanvasKitRenderer(
        canvas = canvas,
        onReady = {
            firstFrameDrawn = true
            notifyReady()
        },
        onUnavailable = { /* 降级细节已在 renderer 内打日志，页面仍为纯色 */ }
    )

    init {
        renderer.setScene(clearScene(clearColor))
    }

    override val ele: HTMLCanvasElement
        get() = canvas

    override fun setProp(propKey: String, propValue: Any): Boolean {
        return when (propKey) {
            CLEAR_COLOR -> {
                clearColor = propValue.unsafeCast<String>()
                if (parseWebColor(clearColor) != null) {
                    renderer.setScene(clearScene(clearColor))
                }
                true
            }

            SCENE -> {
                renderer.setScene(propValue.unsafeCast<String>())
                true
            }

            BACKEND -> {
                renderer.setBackend(propValue.unsafeCast<String>())
                true
            }

            ON_READY -> {
                readyCallback = propValue.unsafeCast<KuiklyRenderCallback>()
                notifyReady()
                true
            }

            KRCssConst.FRAME -> {
                val frame = propValue.unsafeCast<Frame>()
                ele.setFrame(frame, ele.style)
                renderer.resize(frame.width.toInt(), frame.height.toInt())
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
                renderer.setScene(clearScene(clearColor))
                true
            }

            else -> super.resetProp(propKey)
        }
    }

    private fun notifyReady() {
        if (!firstFrameDrawn || readyNotified) {
            return
        }
        readyNotified = true
        readyCallback?.invoke(mapOf("viewName" to VIEW_NAME))
    }

    /** 兼容旧的 clearColor 属性：等价于只包含一个 clear op 的场景 */
    private fun clearScene(color: String): String =
        """{"ops":[{"op":"clear","color":"$color"}]}"""

    companion object {
        const val VIEW_NAME = "CanvasGLView"
        private const val CLEAR_COLOR = "clearColor"
        private const val SCENE = "scene"
        private const val BACKEND = "backend"
        private const val ON_READY = "onReady"
        private const val DEFAULT_CLEAR_COLOR = "#FFFF0000"
    }
}