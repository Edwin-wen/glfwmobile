package com.edwin.glfwmobile.canvas

/**
 * 跨端绘制指令（display list）的唯一构造入口。
 *
 * 设计要点：
 * - 这里只描述「画什么」，不描述「怎么画」。同一份 JSON 会同时喂给
 *   原生端 `canvasCore`（C++ / Skia）和 Web 端 `CanvasKitPlayer`，
 *   两边逐条 op 对齐，保证五端像素一致。
 * - 坐标一律使用逻辑像素（dp / pt / vp / CSS px），物理像素换算由各端在
 *   设置画布尺寸时通过 dpr 传入，业务侧不需要关心屏幕密度。
 *
 * 用法：
 * ```
 * CanvasGLView {
 *     attr {
 *         scene(canvasScene {
 *             clear(0xFFFF0000)
 *             rect(24f, 24f, 120f, 64f, 0xFF23D3FD)
 *         })
 *     }
 * }
 * ```
 */
internal class CanvasScene private constructor(internal val json: String) {

    internal class Builder {
        private val ops = StringBuilder()
        private var count = 0

        fun clear(color: Long): Builder = append("clear", "color" to color.toArgbHex().jsonString())

        fun rect(x: Float, y: Float, w: Float, h: Float, color: Long): Builder =
            append(
                "rect",
                "x" to x.toJsonNumber(),
                "y" to y.toJsonNumber(),
                "w" to w.toJsonNumber(),
                "h" to h.toJsonNumber(),
                "color" to color.toArgbHex().jsonString()
            )

        fun line(
            x1: Float,
            y1: Float,
            x2: Float,
            y2: Float,
            width: Float,
            color: Long
        ): Builder = append(
            "line",
            "x1" to x1.toJsonNumber(),
            "y1" to y1.toJsonNumber(),
            "x2" to x2.toJsonNumber(),
            "y2" to y2.toJsonNumber(),
            "width" to width.toJsonNumber(),
            "color" to color.toArgbHex().jsonString()
        )

        /** 每个 op 都以 `"op":"<name>"` 开头，字段顺序固定，便于跨端 diff */
        private fun append(op: String, vararg fields: Pair<String, String>): Builder {
            if (count++ > 0) {
                ops.append(',')
            }
            ops.append("{\"op\":\"").append(op).append('"')
            fields.forEach { (key, value) ->
                ops.append(",\"").append(key).append("\":").append(value)
            }
            ops.append('}')
            return this
        }

        internal fun build(): CanvasScene = CanvasScene("""{"ops":[$ops]}""")
    }

    companion object {
        /** 单一 clear op 的快捷构造，等价于 `canvasScene { clear(color) }` */
        fun clear(color: Long): CanvasScene = Builder().clear(color).build()
    }
}

internal fun canvasScene(block: CanvasScene.Builder.() -> Unit): CanvasScene =
    CanvasScene.Builder().apply(block).build()

/** 0xAARRGGBB -> "#AARRGGBB"，与 C++ 侧 ParseColor 的 8 位分支对齐 */
private fun Long.toArgbHex(): String {
    val value = this and 0xFFFFFFFFL
    return "#" + value.toString(16).uppercase().padStart(8, '0')
}

/** JSON 数字不接受 "10.0" 之外的平台差异，这里统一去掉无意义的小数位 */
private fun Float.toJsonNumber(): String {
    val asInt = toInt()
    return if (this == asInt.toFloat()) asInt.toString() else toString()
}

private fun String.jsonString(): String = "\"$this\""