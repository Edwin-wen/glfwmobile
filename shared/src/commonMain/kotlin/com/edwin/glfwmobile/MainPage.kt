package com.edwin.glfwmobile

import com.edwin.glfwmobile.base.BasePager
import com.tencent.kuikly.core.annotations.Page
import com.tencent.kuikly.core.base.Color
import com.tencent.kuikly.core.base.ViewBuilder
import com.tencent.kuikly.core.module.RouterModule
import com.tencent.kuikly.core.views.View
import com.tencent.kuikly.core.views.compose.Button

@Page("main", supportInLocal = true)
internal class MainPage : BasePager() {

    override fun body(): ViewBuilder {
        val ctx = this
        return {
            attr {
                backgroundColor(Color.WHITE)
            }
            View {
                attr {
                    size(pagerData.pageViewWidth, pagerData.pageViewHeight)
                    allCenter()
                }
                Button {
                    attr {
                        size(BUTTON_WIDTH, BUTTON_HEIGHT)
                        borderRadius(BUTTON_HEIGHT / 2f)
                        backgroundColor(Color(0xFF23D3FD))
                        titleAttr {
                            text(BUTTON_TEXT)
                            fontSize(17f)
                            color(Color.WHITE)
                        }
                    }
                    event {
                        click {
                            ctx.openCanvasPage()
                        }
                    }
                }
            }
        }
    }

    /**
     * 跳转到原生 OpenGL 画布页
     */
    private fun openCanvasPage() {
        acquireModule<RouterModule>(RouterModule.MODULE_NAME).openPage(CANVAS_PAGE_NAME)
    }

    companion object {
        const val CANVAS_PAGE_NAME = "canvas"
        const val BUTTON_TEXT = "进入画布"
        const val BUTTON_WIDTH = 180f
        const val BUTTON_HEIGHT = 48f
    }
}
