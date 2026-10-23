package com.edwin.glfwmobile.canvas

/**
 * Web 两端（H5 / 小程序）共用的 scene 解析结果。
 *
 * 与原生端 `cppCore/canvasCore/src/scene.cpp` 保持逐条 op 对齐：
 * 同一份 JSON 在原生侧由 Skia 播放，在 Web 侧由 CanvasKit / Canvas2D 播放。
 * 这里只做「读到什么」，不含任何绘制调用。
 */
public sealed interface WebSceneOp {
    data class Clear(val color: Long) : WebSceneOp

    data class Rect(
        val x: Float,
        val y: Float,
        val w: Float,
        val h: Float,
        val color: Long
    ) : WebSceneOp

    data class Line(
        val x1: Float,
        val y1: Float,
        val x2: Float,
        val y2: Float,
        val width: Float,
        val color: Long
    ) : WebSceneOp
}

/** 解析失败返回 null，调用方保留上一帧 */
public fun parseWebScene(json: String): List<WebSceneOp>? {
    return try {
        val root: dynamic = js("JSON.parse(json)")
        val rawOps: dynamic = root.ops
        if (rawOps == null) {
            return null
        }
        val count = (rawOps.length as Number).toInt()
        val ops = ArrayList<WebSceneOp>(count)
        for (i in 0 until count) {
            val raw: dynamic = rawOps[i]
            val color = parseWebColor(raw.color as String) ?: return null
            when (raw.op as String) {
                "clear" -> ops.add(WebSceneOp.Clear(color))
                "rect" -> ops.add(
                    WebSceneOp.Rect(
                        x = (raw.x as Number).toFloat(),
                        y = (raw.y as Number).toFloat(),
                        w = (raw.w as Number).toFloat(),
                        h = (raw.h as Number).toFloat(),
                        color = color
                    )
                )

                "line" -> ops.add(
                    WebSceneOp.Line(
                        x1 = (raw.x1 as Number).toFloat(),
                        y1 = (raw.y1 as Number).toFloat(),
                        x2 = (raw.x2 as Number).toFloat(),
                        y2 = (raw.y2 as Number).toFloat(),
                        width = (raw.width as Number).toFloat(),
                        color = color
                    )
                )

                else -> return null
            }
        }
        ops
    } catch (e: dynamic) {
        null
    }
}

/** `#RGB` / `#RRGGBB` / `#AARRGGBB` -> 0xAARRGGBB，与 C++ 侧 ParseColor 一致 */
public fun parseWebColor(text: String): Long? {
    val hex = text.removePrefix("#")
    val normalized = when (hex.length) {
        3 -> "FF" + hex.map { "$it$it" }.joinToString("")
        6 -> "FF$hex"
        8 -> hex
        else -> return null
    }
    return normalized.toLongOrNull(16)
}

/** 0xAARRGGBB -> (r, g, b, a)，分量 0f~1f */
public fun Long.toRgba(): FloatArray {
    val value = this and 0xFFFFFFFFL
    return floatArrayOf(
        ((value shr 16) and 0xFF) / 255f,
        ((value shr 8) and 0xFF) / 255f,
        (value and 0xFF) / 255f,
        ((value shr 24) and 0xFF) / 255f
    )
}

/** 0xAARRGGBB -> "rgba(r, g, b, a)"，用于 Canvas2D / CSS 兜底 */
public fun Long.toCssRgba(): String {
    val rgba = toRgba()
    return "rgba(${(rgba[0] * 255).toInt()}, ${(rgba[1] * 255).toInt()}, " +
        "${(rgba[2] * 255).toInt()}, ${rgba[3]})"
}

/** 取场景里的第一个 clear 颜色，用于 CanvasKit 不可用时的纯色兜底 */
public fun List<WebSceneOp>.firstClearColor(): Long? =
    firstNotNullOfOrNull { op -> (op as? WebSceneOp.Clear)?.color }