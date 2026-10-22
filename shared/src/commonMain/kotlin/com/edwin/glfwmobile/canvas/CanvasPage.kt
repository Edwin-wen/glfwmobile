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
            // 铺满整页的 Skia 画布
            CanvasGLView {
                attr {
                    absolutePositionAllZero()
                    size(ctx.pagerData.pageViewWidth, ctx.pagerData.pageViewHeight)
                    // 唯一的绘制意图：原生端由 canvasCore(C++/Skia) 播放，Web 端由 CanvasKit 播放
                    scene(CanvasScene.clear(0xFFFF0000))
                }
            }
        }
    }
}
