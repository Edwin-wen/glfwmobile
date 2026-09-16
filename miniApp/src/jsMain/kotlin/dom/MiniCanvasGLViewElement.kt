package dom

import com.tencent.kuikly.core.render.web.runtime.miniapp.MiniGlobal
import com.tencent.kuikly.core.render.web.runtime.miniapp.core.NativeApi
import com.tencent.kuikly.core.render.web.runtime.miniapp.dom.MiniElement
import com.tencent.kuikly.core.render.web.runtime.miniapp.dom.MiniElementUtil

/**
 * Mini program canvas node (type="webgl")
 *
 * Reuses the canvas template alias ({_num: '15'}) built into the mini program runtime,
 * so there is no need to add a new template to `base.wxml`,
 * only the `type` attribute needs to be set to `webgl`.
 */
class MiniCanvasGLViewElement(
    nodeName: String = NODE_NAME,
    nodeType: Int = MiniElementUtil.ELEMENT_NODE
) : MiniElement(nodeName, nodeType) {

    /**
     * Canvas type, `webgl` means using the WebGL context
     */
    var canvasType: String
        get() = getAttribute(TYPE).unsafeCast<String?>() ?: ""
        set(value) {
            setAttribute(TYPE, value)
        }

    /**
     * Get the real canvas node through SelectorQuery,
     * the callback receives null when the node has not been rendered yet
     */
    fun queryCanvasNode(onResult: (dynamic) -> Unit) {
        createSelectorQuery().select("#$innerId").node().exec { result ->
            if (result == null || result[0] == null) {
                onResult(null)
            } else {
                onResult(result[0].node)
            }
        }
    }

    /**
     * Get the SelectorQuery context of the current node;
     * if the node is inside a custom-wrapper, the component instance context must be used
     */
    private fun createSelectorQuery(): dynamic {
        var wrapperElement: MiniElement? = null
        var parent = parentElement.unsafeCast<MiniElement?>()
        while (parent != null) {
            if (parent.needCustomWrapper == true) {
                wrapperElement = parent
                break
            }
            parent = parent.parentElement.unsafeCast<MiniElement?>()
        }
        val context = if (wrapperElement != null) {
            MiniGlobal.globalThis.customWrapperCache.get("custom-${wrapperElement.innerId}")
        } else {
            NativeApi.plat
        }
        return context.createSelectorQuery()
    }

    companion object {
        const val NODE_NAME = "canvas"
        const val TYPE_WEBGL = "webgl"
        private const val TYPE = "type"
    }
}
