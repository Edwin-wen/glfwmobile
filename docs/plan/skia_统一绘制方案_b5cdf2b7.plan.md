---
name: Skia 统一绘制方案
overview: 五端绘制逻辑收敛到同一份「场景指令（scene ops）」，原生三端共用一份 C++ Skia 绘制库（C ABI），H5 / 小程序用同源 Skia 的 CanvasKit 播放同一份指令；平台侧只保留「表面 + 桥接」，先 CPU 光栅化打通，再切 GPU 后端。
todos:
  - id: skia-artifacts
    content: 产出 Android/iOS 的 libskia 构建脚本与产物，鸿蒙做 go/no-go spike
    status: pending
  - id: abi-and-scene
    content: 定义 canvas_core.h C ABI、Scene ops 模型与 JSON 编码
    status: pending
  - id: android
    content: Android 换 CanvasSkiaView + externalNativeBuild，接 canvas_core
    status: pending
  - id: ios
    content: iOS 改 .mm + 本地 pod 接入 Skia.xcframework 与 canvas_core
    status: pending
  - id: ohos
    content: 鸿蒙 CMake 链接 canvas_core，XComponent 表面改为纹理贴图
    status: pending
  - id: h5
    content: H5 接 CanvasKit 播放 scene，处理 wasm 资源分发
    status: pending
  - id: miniapp
    content: 小程序 WXWebAssembly 接 CanvasKit，含 Canvas2D 降级
    status: pending
  - id: gpu-backend
    content: Metal/EGL GPU 后端，raster 保留兜底
    status: pending
  - id: golden-tests
    content: raster golden 测试与五端截图 diff 验收
    status: pending
isProject: false
---

# 一、先说清楚一个硬约束

「一份 C++ 跑五端」在纯原生端成立，在 Web 端不成立：H5 / 小程序没有 C++ 运行时（小程序只有 `WXWebAssembly`，且主包 1.5MB 上限、`miniApp/dist` 已经被压缩到 1.4MB）。CanvasKit 就是 Skia 的 WASM 构建，API 同源。

所以统一的是**「画什么」（scene ops 唯一事实源）**，「怎么画」保留两个薄播放器：C++ Skia（原生三端）、CanvasKit（Web 两端）。后续若要连播放器都合并，可把同一个 C++ painter 用 Emscripten 编进自建 CanvasKit，属于升级路径，不在第一期。

```
                        shared/commonMain
                   Scene DSL（唯一绘制意图）
                   ops: clear / rect / line / path / text
                              │  编码成 JSON
              ┌───────────────┴────────────────┐
              ▼                                ▼
   prop "scene"（原生三端）            prop "scene"（Web 两端）
   Android / iOS / 鸿蒙                 H5 / 微信小程序
              │ skc_set_scene(json)            │
              ▼                                ▼
   canvasCore（C++ / Skia）             CanvasKit（Skia WASM）
   ├ SceneParser（jsmn）
   ├ Painter     ops → SkCanvas   ◄── 同一份 op 语义，逐条对齐
   └ Surface     Raster(一期) / Metal·EGL(阶段5)
```

# 二、模块划分（职责与位置）

- `canvasCore/include/canvas_core.h` — **唯一跨语言契约**，只用 C 类型，不暴露 Skia 头文件；平台侧不需要 `#include <skia/...>`
- `canvasCore/src/scene.{h,cpp}` — display list 解析（vendored jsmn，约 200 行，避免给五端引 JSON 库）
- `canvasCore/src/painter.{h,cpp}` — **唯一原生绘制逻辑**（ops → SkCanvas）
- `canvasCore/src/surface_raster.cpp` / `surface_gpu_egl.cpp` / `surface_gpu_metal.mm` — 表面后端
- `canvasCore/prebuilt/<platform>/` — `libskia.a` 产物，**不入库**（脚本产出 + CI 缓存）
- `canvasCore/scripts/build-skia-{android,ios,ohos}.sh` — Skia 构建，版本钉死并记 checksum
- [shared/src/commonMain/kotlin/com/edwin/glfwmobile/canvas/Scene.kt](shared/src/commonMain/kotlin/com/edwin/glfwmobile/canvas/Scene.kt) — ops 定义 + JSON 编码（列表 + 手写序列化，不引 kotlinx.serialization）
- [shared/src/jsMain/kotlin/canvas/CanvasKitPainter.kt](shared/src/jsMain/kotlin/canvas/CanvasKitPainter.kt) — Web 播放器，H5 / 小程序共用（`shared` 已有 js target）
- 各端 `CanvasGLView` 退化为「setProp → Scene → 桥接调用 + onReady」，不再有任何绘制代码
- 新增 `canvasCore/tests/` — raster 出 PNG + golden hash，作为五端像素基准

C ABI 草图（阶段 1 定型，阶段 5 只加函数不改签名）：

```c
typedef struct skc_canvas skc_canvas;
skc_canvas* skc_create(void);
void        skc_destroy(skc_canvas*);
void        skc_set_size(skc_canvas*, int32_t w, int32_t h, float dpr);
void        skc_set_scene(skc_canvas*, const char* scene_json); /* 解析失败保留上一帧 */
bool        skc_render_raster(skc_canvas*, void* rgba_pixels, int32_t row_bytes);
/* 阶段 5 */
bool        skc_attach_gl(skc_canvas*, void* egl_display, void* egl_surface);
bool        skc_attach_metal(skc_canvas*, void* ca_metal_layer);
bool        skc_render(skc_canvas*);
```

# 三、分阶段落地

## 阶段 0：Skia 产物与骨架（风险前置，先跑通再谈接入）
- Android：`build-skia-android.sh` 用 GN 出 `arm64-v8a` / `x86_64` 的 `libskia.a`（裁剪 codec / effects），strip 后进 `prebuilt/android/<abi>/`；本机已有 NDK 27 + CMake 3.22，无需额外环境
- iOS：`arm64` + `simulatorarm64` 打 `Skia.xcframework`，配本地 pod `canvasCore/canvas_core.podspec` 接入，**不改 `project.pbxproj`**（现有 Podfile 已有 `shared` 本地 pod 先例）
- 鸿蒙：**先做 1~2 天 spike（go/no-go）**，用 OpenHarmony `third_party/skia` 的 `build/ohos.gni` 或 Flutter OH engine 的 gn 配置产出 `libskia.a`
- 验收：脱离 UI，命令行跑 `skc_create → set_scene(clear #FF0000) → render_raster → PNG`

## 阶段 1：契约 + commonMain + Android 参考实现
- [shared/.../canvas/CanvasGLView.kt](shared/src/commonMain/kotlin/com/edwin/glfwmobile/canvas/CanvasGLView.kt) 新增 `scene` 属性；`clearColor` 保留并内部翻译成 `Clear` op（不破坏已完成的三端桥接）
- [CanvasPage.kt](shared/src/commonMain/kotlin/com/edwin/glfwmobile/canvas/CanvasPage.kt) 改为 `scene { clear(...) }` 形式，业务侧只写 DSL
- `canvasCore` 实现 jsmn 解析 + `Clear` 分支
- Android：`androidApp/build.gradle.kts` 加 `externalNativeBuild`；`CanvasGLView` 从 `GLSurfaceView` 换成 `View` + `Bitmap` + `skc_render_raster` + `invalidate()`，注册名仍为 `CanvasGLView`
- 验收：Android `?page_name=canvas` 纯红、`onReady` 仅一次、与旧实现像素一致

## 阶段 2：iOS + 鸿蒙接入（可并行）
- iOS：`CanvasGLView.m` → `.mm`，raster → `CGImage` → `layer.contents`；保留现有「Metal 失败退纯色」的兜底语义（此时兜底由 Skia raster 承担）
- 鸿蒙：`entry/src/main/cpp/CMakeLists.txt` 链接 `canvas_core` + `libskia.a`；现有 XComponent + EGL 表面改为「上传 RGBA 纹理 + 全屏 quad」，为阶段 5 的 `GrDirectContext::MakeGL` 铺路
- 验收：三端喂同一份 scene JSON，截图 diff 一致

## 阶段 3：H5 CanvasKit
- `CanvasKitPainter`：`MakeWebGLCanvasSurface`，失败回退 `MakeSWCanvasSurface`，再失败回退 CSS 纯色（保留现有降级链）
- [h5App/.../components/CanvasGLView.kt](h5App/src/jsMain/kotlin/components/CanvasGLView.kt) 只保留 frame/prop 转发；`canvaskit-wasm` 随 `publishLocalJSBundle` 复制进 `h5App/build/distributions`

## 阶段 4：小程序 CanvasKit（风险最高的一个）
- 先 spike：`WXWebAssembly.instantiate` + `CanvasKitInit({ instantiateWasm })` + `MakeWebGLContext(node.getContext('webgl'))` + `MakeOnScreenGLSurface`（不走 DOM，绕开小程序无 `document/fetch`）
- 包体：`canvaskit.wasm` 放分包，wasm 用 `wx.getFileSystemManager()` 读成 ArrayBuffer
- 降级：不可用时用 Canvas2D 播放器覆盖 `clear/rect/line/path` 子集
- 验收：微信开发者工具 + 真机预览（WebGL 不支持真机调试，沿用现有结论）

## 阶段 5：GPU 后端
- `surface_gpu_metal.mm`：`GrDirectContext::MakeMetal` + `CAMetalLayer`
- `surface_gpu_egl.cpp`：`GrDirectContext::MakeGL`；Android 走 `SurfaceTexture`/`ANativeWindow`，鸿蒙走 `OHNativeWindow`
- 开关式切换（编译期或运行期 capability），raster 永久保留为兜底

# 四、风险与对策

- 鸿蒙 Skia：最大不确定性，spike 前置；不通则鸿蒙**不影响其他四端**，保留现有 EGL 清屏
- 小程序 WASM：包体 + 兼容性，降级路径已定
- 包体：Skia 静态库 4~8MB/架构，靠裁剪 + strip + 内部制品仓存放，不进 git
- CPU raster 性能：整页 1170×2532 ≈ 11MB/帧，静态画面够用；动效上线前必须切阶段 5
- ABI 稳定：`canvas_core.h` 只允许 C 类型；Kotlin / ArkTS / ObjC 一律不直接绑 Skia

# 五、验收方式

- `canvasCore/tests`：raster 出 PNG + golden hash，五端共用同一批基准图
- 端到端：`?page_name=canvas` 各端截图 diff，阈值化比较；`onReady` 语义统一为「首个 Skia 帧已呈现」