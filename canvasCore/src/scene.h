/*
 * scene —— display list 的数据模型与解析。
 *
 * 该模块刻意不依赖 Skia，便于在宿主机上单独编译测试解析逻辑；
 * 绘制语义由 painter 负责，两边共享同一份 Op 定义。
 */
#ifndef CANVAS_CORE_SCENE_H
#define CANVAS_CORE_SCENE_H

#include <cstdint>
#include <vector>

namespace skc {

/** 0xAARRGGBB，与 Kuikly / Android 的 Color 顺序一致 */
using Color = uint32_t;

enum class OpType {
    kClear,
    kFillRect,
    kStrokeLine,
};

struct Rect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

struct Line {
    float x1 = 0;
    float y1 = 0;
    float x2 = 0;
    float y2 = 0;
    float width = 1;
};

/** 一条绘制指令；只有与 type 对应的字段有意义 */
struct Op {
    OpType type = OpType::kClear;
    Color color = 0xFFFF0000;
    Rect rect;
    Line line;
};

struct Scene {
    std::vector<Op> ops;

    bool empty() const { return ops.empty(); }
    void clear() { ops.clear(); }
};

/**
 * 解析 `#RGB` / `#RRGGBB` / `#AARRGGBB`（也接受不带 # 的形式）。
 * 失败返回 false 且不写 out。
 */
bool ParseColor(const char *text, size_t length, Color *out);

/**
 * 解析场景 JSON：
 *   {"ops":[{"op":"clear","color":"#FFFF0000"},
 *           {"op":"rect","x":0,"y":0,"w":10,"h":10,"color":"#FF00FF00"},
 *           {"op":"line","x1":0,"y1":0,"x2":10,"y2":10,"width":2,"color":"#FF0000FF"}]}
 *
 * 整份 JSON 必须完全合法才生效：任意一条 op 非法则返回 false，out 保持原样，
 * 对应「解析失败保留上一帧」的语义。
 */
bool ParseScene(const char *json, Scene *out);

/** 供错误信息使用：把最后一个非法 op 的名字写进 name（失败时为空） */
const char *LastSceneError();

} // namespace skc

#endif // CANVAS_CORE_SCENE_H