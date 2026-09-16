package com.edwin.glfwmobile.canvas

import com.edwin.glfwmobile.base.BasePager
import com.tencent.kuikly.core.annotations.Page
import com.tencent.kuikly.core.base.Color
import com.tencent.kuikly.core.base.ViewBuilder

@Page("canvas", supportInLocal = true)
internal class CanvasPage : BasePager() {

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                backgroundColor(Color.BLACK)
            }
            // 铺满整页的原生 OpenGL 视图
            // 自定义 View 必须显式指定宽高，这里用绝对定位 + 页面尺寸保证铺满且不参与 flex 布局
            CanvasGLView {
                attr {
                    absolutePositionAllZero()
                    size(ctx.pagerData.pageViewWidth, ctx.pagerData.pageViewHeight)
                }
            }
        }
    }
}
