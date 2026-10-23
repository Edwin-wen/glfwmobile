/*
 * scene 解析器宿主机单测（不依赖 Skia，可直接用 clang++ 编译运行）：
 *   clang++ -std=c++17 -IcppCore/canvasCore/src -IcppCore/canvasCore/third_party \
 *       cppCore/canvasCore/tests/scene_test.cpp cppCore/canvasCore/src/scene.cpp \
 *       -o /tmp/scene_test && /tmp/scene_test
 */
#include <cstdio>
#include <string>

#include "scene.h"

namespace {

int g_failed = 0;

void Expect(bool condition, const char *message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        ++g_failed;
    }
}

void TestColors() {
    skc::Color color = 0;
    Expect(skc::ParseColor("#FFFF0000", 9, &color) && color == 0xFFFF0000, "8-digit color");
    Expect(skc::ParseColor("#00FF00", 7, &color) && color == 0xFF00FF00, "6-digit color");
    Expect(skc::ParseColor("#F00", 4, &color) && color == 0xFFFF0000, "3-digit color");
    Expect(!skc::ParseColor("#GG0000", 7, &color), "invalid hex rejected");
    Expect(!skc::ParseColor("", 0, &color), "empty color rejected");
}

void TestClearScene() {
    skc::Scene scene;
    const bool ok = skc::ParseScene(R"({"ops":[{"op":"clear","color":"#FFFF0000"}]})", &scene);
    Expect(ok, "clear scene parses");
    Expect(scene.ops.size() == 1, "clear scene has one op");
    Expect(scene.ops[0].type == skc::OpType::kClear, "clear op type");
    Expect(scene.ops[0].color == 0xFFFF0000, "clear op color");
}

void TestShapeScene() {
    skc::Scene scene;
    const bool ok = skc::ParseScene(
        R"({"ops":[{"op":"clear","color":"#FF000000"},)"
        R"({"op":"rect","x":10,"y":20.5,"w":30,"h":40,"color":"#FF23D3FD"},)"
        R"({"op":"line","x1":0,"y1":0,"x2":100,"y2":100,"width":2,"color":"#80000000"}]})",
        &scene);
    Expect(ok, "shape scene parses");
    Expect(scene.ops.size() == 3, "shape scene has three ops");
    Expect(scene.ops[1].type == skc::OpType::kFillRect && scene.ops[1].rect.y == 20.5f, "rect fields");
    Expect(scene.ops[2].type == skc::OpType::kStrokeLine && scene.ops[2].line.width == 2.0f,
           "line fields");
}

void TestRejectsInvalid() {
    skc::Scene scene;
    Expect(!skc::ParseScene("", &scene), "empty rejected");
    Expect(!skc::ParseScene(R"({"ops":[{"op":"clear","color":"#FFFF0000"})", &scene),
           "truncated json rejected");
    Expect(!skc::ParseScene(R"({"ops":[{"op":"clear","color":"red"}]})", &scene),
           "bad color rejected");
    Expect(!skc::ParseScene(R"({"ops":[{"op":"rect","x":1,"y":2,"w":3,"color":"#FFFF0000"}]})",
                            &scene),
           "rect without h rejected");
    Expect(!skc::ParseScene(R"({"ops":[{"op":"unknown","color":"#FFFF0000"}]})", &scene),
           "unknown op rejected");
    Expect(skc::ParseScene(R"({"ops":[]})", &scene), "empty ops is a valid scene");
    Expect(!skc::ParseScene(R"({"foo":1})", &scene), "missing ops rejected");
}

/** 解析失败必须保持上一帧：out 不被改写 */
void TestKeepsPreviousSceneOnFailure() {
    skc::Scene scene;
    Expect(skc::ParseScene(R"({"ops":[{"op":"clear","color":"#FF000000"}]})", &scene), "seed scene");
    const size_t before = scene.ops.size();
    Expect(!skc::ParseScene("not json", &scene), "garbage rejected");
    Expect(scene.ops.size() == before && scene.ops[0].color == 0xFF000000, "scene preserved");
}

} // namespace

int main() {
    TestColors();
    TestClearScene();
    TestShapeScene();
    TestRejectsInvalid();
    TestKeepsPreviousSceneOnFailure();
    if (g_failed == 0) {
        std::printf("all scene tests passed\n");
        return 0;
    }
    std::printf("%d test(s) failed\n", g_failed);
    return 1;
}