# Kuikly 原生画布（CanvasGLView）跨端实现与验证

> 目标：Kuikly 新增 `main` 页（按钮）→ 跳转 `canvas` 页（整页原生 OpenGL 红色画布），
> 该自定义 View 在 **Android / iOS / 鸿蒙 / H5 / 微信小程序** 五端各自用平台图形能力实现；
> 同时把所有宿主的**默认启动页由 `router` 改为 `main`**。

版本基线：Kotlin `2.1.21`、KSP `2.1.21-2.0.1`、AGP `7.4.2`、Kuikly `2.7.0-2.1.21`。

---

## 零、当前进度（绘制统一到 Skia）

桥接链路已在五端跑通之后，绘制实现收敛到 **Skia**：业务侧只产出一份「场景指令（scene ops）」，
原生端由 `cppCore/canvasCore/`（C++ / Skia）播放，Web 端由 CanvasKit（H5）/ Canvas2D（小程序）播放。

| 端 | 状态 | 实现 | 验证 |
| --- | --- | --- | --- |
| Android | **已迁移** | raster：`View` + `Bitmap`；gpu：`GLSurfaceView` + Ganesh | 模拟器实测两种后端均为纯红 |
| iOS | **已迁移** | raster：`CGImage`；gpu：`CAEAGLLayer` + Ganesh（Skia 产物无 Metal 后端） | `xcodebuild -sdk iphoneos` 编译链接通过（需真机运行） |
| 鸿蒙 | **已迁移** | raster：光栅化 + 纹理上传；gpu：EGL 内直接绘制 | `hvigorw assembleHap` 产出签名 HAP，含两种后端 |
| H5 | **已迁移** | CanvasKit：WebGL / 软件 surface 可切 | 浏览器实测两种后端均为纯红 |
| 小程序 | **已迁移** | `<canvas type="2d">` + Canvas2D（GPU 暂不支持） | 构建通过，需微信开发者工具验证 |

- 后端开关：`CanvasGLView { attr { backend(CanvasBackend.RASTER / GPU / AUTO) } }`，
  默认 AUTO（优先 GPU，失败回退 raster），细节见 9.5

- 契约：`cppCore/canvasCore/include/canvas_core.h`（C ABI）+ `shared/.../canvas/CanvasScene.kt`（scene DSL）
- 已支持 op：`clear` / `rect` / `line`；坐标统一为逻辑像素，物理像素由 dpr 换算
- Web 侧 scene 解析在 `canvasWeb/src/jsMain/.../WebScene.kt`，与 `canvasCore/src/scene.cpp` 逐条对齐
- 小程序不使用 CanvasKit：`WXWebAssembly` + 主包 2MB 限制风险高，先用 Canvas2D 覆盖当前 op 集合
- 下文 4.1–4.5 描述的是**迁移前**的各端实现，作为历史参考；当前实现在文末「九」

---

## 一、总体方案

Kuikly 跨端自定义 View 的机制是：**业务层只声明一个 `viewName`，各端宿主按这个名字注册自己的原生实现**。
因此整个功能只依赖一条纽带 —— 字符串 `"CanvasGLView"`。

```
                    shared（commonMain）
        ┌──────────────────────────────────────────┐
        │  CanvasGLView : DeclarativeBaseView      │
        │  viewName() = "CanvasGLView"  ◄──────────┼── 唯一契约
        └──────────────────────────────────────────┘
                            │
     ┌──────────┬───────────┼───────────┬──────────────┐
     ▼          ▼           ▼           ▼              ▼
  Android      iOS        鸿蒙         H5          小程序
 GLSurface   CAMetalLayer XComponent  <canvas>   <canvas
 View+ES2     +MTLDevice   +native     +WebGL     type="webgl">
                          EGL/GLES3
```

关键约束：**类名（iOS）、注册名（`VIEW_NAME` 常量）、Kuikly 侧 `viewName` 三处必须完全一致**，
否则页面会静默渲染不出来（不抛异常，只在各端日志提示找不到 View）。

### 文件清单

| 层 | 文件 |
| --- | --- |
| 契约 | `shared/src/commonMain/kotlin/com/edwin/glfwmobile/canvas/CanvasGLView.kt` |
| 页面 | `shared/src/commonMain/kotlin/com/edwin/glfwmobile/MainPage.kt` |
| 页面 | `shared/src/commonMain/kotlin/com/edwin/glfwmobile/canvas/CanvasPage.kt` |
| Android | `androidApp/src/main/java/com/edwin/glfwmobile/expandview/CanvasGLView.kt` |
| Android 注册 | `androidApp/src/main/java/com/edwin/glfwmobile/KuiklyRenderActivity.kt` |
| iOS | `iosApp/iosApp/KuiklyExpand/Views/CanvasGLView.h` / `.m`（已加入 `project.pbxproj`） |
| 鸿蒙 | `ohosApp/entry/src/main/ets/kuikly/components/KRCanvasGLView.ets` |
| 鸿蒙 native | `ohosApp/entry/src/main/cpp/canvas_gl_render.{h,cpp}`、`napi_init.cpp`、`CMakeLists.txt` |
| 鸿蒙注册 | `ohosApp/entry/src/main/ets/kuikly/KuiklyViewDelegate.ets` |
| H5 | `h5App/src/jsMain/kotlin/components/CanvasGLView.kt` |
| H5 注册 | `h5App/src/jsMain/kotlin/KuiklyWebRenderViewDelegator.kt` |
| 小程序 | `miniApp/src/jsMain/kotlin/dom/MiniCanvasGLViewElement.kt` |
| 小程序 | `miniApp/src/jsMain/kotlin/components/CanvasGLView.kt` |
| 小程序注册 | `miniApp/src/jsMain/kotlin/KuiklyWebRenderViewDelegator.kt` |

> **设计取舍：尽量不新增平台配置文件。** 五端都复用宿主已有的 View 注册入口；
> 小程序刻意复用运行时内置的 `canvas` 模板别名，**不改动 `miniApp/dist/base.wxml`**（见 4.5）。

---

## 二、跨端协议（属性 / 事件）

Kuikly 侧只暴露一个属性与一个事件，五端实现语义必须一致：

| 名称 | 类型 | 说明 | 默认值 |
| --- | --- | --- | --- |
| `clearColor` | `String` | 清屏色，`#AARRGGBB` / `#RRGGBB`（H5、小程序额外兼容 `#RGB`） | 各端硬编码不透明红 `#FFFF0000` |
| `onReady` | `Event` | GL 首帧绘制完成后回调一次，用于确认画布真的画出来了 | — |

统一语义：**颜色解析失败时保留当前颜色**（不崩溃、不置黑）。

---

## 三、共享层实现思路

### 3.1 CanvasGLView 契约

```kotlin
internal class CanvasGLView : DeclarativeBaseView<CanvasGLViewAttr, CanvasGLViewEvent>() {
    override fun viewName(): String = VIEW_NAME
    companion object {
        /** 组件名，必须与各端 Native 注册名/类名完全一致 */
        const val VIEW_NAME = "CanvasGLView"
    }
}
```

- `createAttr()` / `createEvent()` 分别提供 `CanvasGLViewAttr`（`clearColor` 用 Kuikly 的
  `with` 语法写属性，由渲染层统一序列化成 `setProp(key, value)`）与 `CanvasGLViewEvent`（`onReady`）；
- 额外导出 `ViewContainer<*, *>.CanvasGLView { }` DSL 扩展，保持与内置组件一致的书写习惯。

### 3.2 MainPage

```kotlin
@Page("main", supportInLocal = true)
internal class MainPage : BasePager() {
    // 白底铺满 + allCenter()，居中一个圆角按钮
    private fun openCanvasPage() {
        acquireModule<RouterModule>(RouterModule.MODULE_NAME).openPage(CANVAS_PAGE_NAME)
    }
    companion object { const val CANVAS_PAGE_NAME = "canvas" }
}
```

- 按钮：`size(180f, 48f)` + `borderRadius(24f)` + `backgroundColor(Color(0xFF23D3FD))`，文案"进入画布"；
- 跳转统一走 `RouterModule`，常量 `CANVAS_PAGE_NAME = "canvas"` 与 `@Page("canvas")` 必须对齐。

### 3.3 CanvasPage（铺满整页的两个坑）

```kotlin
CanvasGLView {
    attr {
        absolutePositionAllZero()
        size(ctx.pagerData.pageViewWidth, ctx.pagerData.pageViewHeight)
    }
}
```

1. **自定义 View 必须显式设置宽高**。它没有内置组件的默认尺寸，不调 `size(w, h)` 会出现
   "注册成功但看不见"。
2. **用绝对定位 + 页面尺寸**，而不是让它参与 flex 布局：既能铺满，又不会因为自身尺寸参与测量
   与父容器形成循环约束。

---

## 四、各端原生实现思路

### 4.1 Android：GLSurfaceView + OpenGL ES 2.0

文件：`androidApp/.../expandview/CanvasGLView.kt`

- 直接继承 `GLSurfaceView` 并实现 `IKuiklyRenderViewExport`，交给 Kuikly 布局体系托管。
- 画面是**静态纯色**，所以用 `RENDERMODE_WHEN_DIRTY` 按需渲染，只在颜色变化 / `onSurfaceChanged`
  时 `requestRender()`，避免默认 60fps 空转的功耗：

```kotlin
init {
    setEGLContextClientVersion(2)
    setRenderer(glRenderer)
    renderMode = RENDERMODE_WHEN_DIRTY
}
```

- `clearColor` 用 `@Volatile` 字段跨线程传给 GL 线程；`applyClearColor()` 只允许在 GL 线程调用。
- `onReady` 从 GL 线程 `post` 回主线程，保证 Kuikly 侧事件回调线程一致；
  用 `firstFrameDrawn` + `readyEventFired` 双标志保证**只回调一次**。
- 实现 `resetProp`：属性被移除时回落到默认红。

注册（踩坑点）：

```kotlin
override fun registerExternalRenderView(kuiklyRenderExport: IKuiklyRenderExport) {
    super.registerExternalRenderView(kuiklyRenderExport)
    with(kuiklyRenderExport) {
        // 注意：renderViewExport 必须把 creator 作为参数传入，不要使用尾随 lambda 语法
        renderViewExport(CanvasGLView.VIEW_NAME, { context -> CanvasGLView(context) })
    }
}
```

> `renderViewExport` 最后一个参数是 `creator`。写成尾随 lambda 时 Kotlin 会绑定到别的重载上，
> 导致注册悄悄失效——**编译能过，运行时找不到 View**。

已知限制：`GLSurfaceView` 用独立 Surface 渲染，不支持圆角与半透明叠加，适合本场景的整页铺满。

### 4.2 iOS：UIView + CAMetalLayer / Metal

文件：`iosApp/iosApp/KuiklyExpand/Views/CanvasGLView.{h,m}`

- 类名必须**恰好**是 `CanvasGLView`（Kuikly iOS 侧按类名查找，没有独立注册表）。
- 用 `+ (Class)layerClass` 把 backing layer 换成 `CAMetalLayer`。
- **为什么换 Metal**：OpenGL ES 在 iOS 已废弃（deprecated since iOS 12，能跑但不再优化），Metal 是
  Apple 官方推荐方案。本实现用 Metal 完成与原来等价的"清屏 + present"逻辑，接口契约（`clearColor` /
  `onReady`）完全不变。
- 属性/事件靠**命名约定**自动桥接：`css_clearColor` ←→ `attr { clearColor(...) }`，
  `css_onReady` ←→ `event { onReady { } }`，`setProp` 用宏统一转发：

```objc
- (void)hrv_setPropWithKey:(NSString *)propKey propValue:(id)propValue {
    KUIKLY_SET_CSS_COMMON_PROP;   // 分发到 setCss_xxx:
}
```

- **懒创建 Metal 设备**，放在 `layoutSubviews`（保证有真实 frame 后再建，且在主线程）。
  `MTLCreateSystemDefaultDevice()` 拿到 `_device` 后创建 `MTLCommandQueue`。
- **drawable 尺寸必须与物理像素对齐**，尺寸变化时更新 `drawableSize`，否则画面被拉伸：

```objc
CGFloat scale = MAX(self.contentScaleFactor, _metalLayer.contentsScale);
CGSize drawableSize = CGSizeMake(size.width * scale, size.height * scale);
if (!CGSizeEqualToSize(drawableSize, _lastDrawableSize)) {
    _metalLayer.drawableSize = drawableSize;
    _lastDrawableSize = drawableSize;
}
```

- 清屏实现：`nextDrawable` → 建 `MTLRenderPassDescriptor`（`loadAction = Clear`，
  `clearColor = 当前色`）→ 空 `MTLRenderCommandEncoder` → `presentDrawable` + `commit`。
  静态画面**不跑 60fps 渲染循环**，只在颜色/尺寸变化时重绘一次。
- 三处**兜底**保证任何 Metal 失败路径都不白屏：① 初始化时先用 `backgroundColor` 铺成当前清屏色；
  ② `MTLCreateSystemDefaultDevice()` 失败保持纯色；③ `nextDrawable` 拿不到（窗口不可见等）保持纯色。
- `Metal.framework` 依赖 `CLANG_ENABLE_MODULES = YES` 自动链接，**无需改 `project.pbxproj`**；
  `CanvasGLView.h/.m` 已写入 `project.pbxproj` 的 `PBXFileReference` + `PBXGroup` + `PBXSourcesBuildPhase` 三处。

### 4.3 鸿蒙：XComponent(SURFACE) + native EGL / GLES3

文件：`KRCanvasGLView.ets` + `entry/src/main/cpp/canvas_gl_render.cpp`

- ArkTS 侧继承 `KuiklyRenderBaseView`，用 `ComponentContent` + `wrapBuilder` 返回承载 UI。
- UI 本体是 `XComponent(SURFACE, libraryname: 'entry')`，真正绘制在 native 用 EGL + GLES3 完成。
- **Surface 生命周期是核心难点**：`setProp(clearColor)` 可能早于 Surface 创建，所以颜色不直接画，
  而是通过 NAPI 传给 native **缓存**，等 Surface 就绪后首帧生效：

```ts
applyClearColorToNative(): void {
  const color = this.cssClearColor;
  if (color == null || color.length == 0) { return; }
  try { nativeEntry.setCanvasClearColor(this.xComponentId, color); }
  catch (e) { console.error(`... setCanvasClearColor failed: ${JSON.stringify(e)}`); }
}
```

```ts
XComponent({ id: this.renderView.xComponentId, type: XComponentType.SURFACE, libraryname: 'entry' })
  .width('100%').height('100%')
  .onLoad(() => {
    this.renderView.applyClearColorToNative();   // Surface 已创建：同步颜色
    this.renderView.fireReady();                 // 通知 Kuikly 画布就绪
  })
```

- **每个实例有唯一 XComponent id**（`krcanvasgl_${idSeed++}`），同页多画布互不干扰，
  也是 ArkTS 与 native 按 id 定位渲染上下文的键。
- native 侧通过 `OH_NativeXComponent` 回调接 `OnSurfaceCreated` / `OnSurfaceChanged` /
  `OnSurfaceDestroyed`，静态画面只在创建/尺寸变化时清屏一次，不做常驻刷新。
- 构建接入 `CMakeLists.txt`：

```cmake
add_library(entry ../shared napi_init.cpp canvas_gl_render.cpp)
target_link_libraries(entry PUBLIC libace_napi.z.so libace_ndk.z.so libhilog_ndk.z.so
        libEGL.so libGLESv3.so libnative_window.so kuikly_shared kuikly_render)
```

注册：`KuiklyViewDelegate.ets` 的 `getCustomRenderViewCreatorRegisterMap()` 中
`map.set(KRCanvasGLView.VIEW_NAME, () => new KRCanvasGLView())`。

### 4.4 H5：`<canvas>` + WebGL

文件：`h5App/src/jsMain/kotlin/components/CanvasGLView.kt`

- 实现 `IKuiklyRenderViewExport`，`ele` 直接返回 `document.createElement("canvas")`。
- 布局交给 Kuikly；`Frame` 变化时把 **CSS 尺寸和绘制缓冲区尺寸一起同步**，否则 WebGL 画面被拉伸：

```kotlin
KRCssConst.FRAME -> {
    val frame = propValue.unsafeCast<Frame>()
    ele.setFrame(frame, ele.style)
    // canvas 的绘制缓冲区尺寸需要与布局尺寸保持一致，否则 WebGL 画面会被拉伸
    ele.width = frame.width.toInt().coerceAtLeast(1)
    ele.height = frame.height.toInt().coerceAtLeast(1)
    render(); onFrameChange(frame); true
}
```

- 上下文双保险：`webgl` → `experimental-webgl`；都拿不到时**降级为 CSS 纯色背景**并照常
  `notifyReady()`，避免整块区域白屏。
- 静态画面只在颜色/尺寸变化时 `clear` 一次，不跑 `requestAnimationFrame` 循环。

注册：`h5App/.../KuiklyWebRenderViewDelegator.kt` 中
`renderViewExport(CanvasGLView.VIEW_NAME, { CanvasGLView() })`。

### 4.5 微信小程序：`<canvas type="webgl">`

文件：`miniApp/src/jsMain/kotlin/dom/MiniCanvasGLViewElement.kt` + `components/CanvasGLView.kt`

小程序的难点是**节点类型必须落在模板白名单里**。两条路：① 在 `base.wxml` 新增自定义模板；
② 复用运行时已内置的 `canvas` 模板。本次选 **②**，因为 `base.wxml` / `comp.wxml` 属于
**构建生成物**，手改会在下次构建被覆盖；而运行时 `Transform` 已内置 canvas 别名：

```kotlin
// com.tencent.kuikly.core.render.web.runtime.miniapp.core.Transform
canvas: {_num: '15', canvasId: 'p0', disableScroll: 'p1', type: 'p2'},
```

它对应 `base.wxml` 已存在的 `tmpl_0_15`，模板里本来就有 `type="{{i.p2}}"` 绑定：

```wxml
<template name="tmpl_0_15">
  <canvas canvas-id="{{i.p0}}" disable-scroll="{{xs.b(i.p1,!1)}}" ... type="{{i.p2}}" ... />
```

所以**只要把 `type` 设成 `webgl` 就能拿到 WebGL canvas，一行 wxml 都不用改**：

```kotlin
init {
    // A WebGL canvas must declare its type explicitly
    canvasElement.canvasType = MiniCanvasGLViewElement.TYPE_WEBGL
}
```

> `canvas-id`：官方文档说明"若指定了 type 则无需再指定 canvas-id"。本实现**刻意不设 `canvasId`**，
> 模板里 `i.p0` 为 undefined、最终渲染为空串，不会占用"同页不可重复"的 `canvas-id` 命名空间，
> 符合新接口（`type`）用法。

另外两个小程序特有的坑：

1. **canvas 节点异步渲染**，`setProp` 时可能还查不到节点 → 用带次数上限的 `SelectorQuery.node()`
   重试（20 次 × 50ms），超限后降级纯色背景，绝不无限重试。
2. **`createSelectorQuery()` 必须落在正确上下文**：节点若在 `custom-wrapper` 内，必须用对应组件实例的
   query（`customWrapperCache`），用全局 query 会查不到。`MiniCanvasGLViewElement` 会向上遍历祖先找
   `needCustomWrapper == true` 的节点，否则退回 `NativeApi.plat`。

---

## 五、默认启动页统一为 main

| 平台 | 文件 | 改动 |
| --- | --- | --- |
| Android | `androidApp/.../KuiklyRenderActivity.kt` | `DEFAULT_PAGE_NAME = "main"`（intent 未传 `pageName` 时生效） |
| iOS | `iosApp/iosApp/ContentView.swift` | `KuiklyRenderViewPage(pageName: "main", data: [:])` |
| 鸿蒙 | `ohosApp/entry/src/main/ets/pages/Index.ets` | `pagerName: this.pageName ?? 'main'` |
| H5 | `h5App/src/jsMain/kotlin/Main.kt` | 无 `page_name` 参数时 `?: "main"` |
| 小程序 | `miniApp/dist/pages/index/index.js` | `render.renderView({ pageName: "main" })` |

注意 H5 是**参数默认值**而非唯一出口，仍支持 `?page_name=xxx` 覆盖以便调试任意页面；
小程序 `pages/index/index.js` 是页面入口脚本（非构建产物），改它就是改默认页。

---

## 六、各平台验证方法

### 6.0 通用检查清单（先看这个）

1. **名称三处一致**：Kuikly `viewName()` == 宿主注册名 == 原生类名。
2. **是否显式设置了宽高**：见 3.3，尺寸为 0 就是白屏。
3. **是否重新构建了共享层**：`shared` 的 `@Page` 由 **KSP** 生成页面注册表（`KuiklyCoreEntry`）。
   改页面/组件后**必须重新构建 shared**，否则宿主加载的是旧注册表，会报找不到页面。
4. `gradle.properties` 里 `ksp.incremental=false`，每次全量生成，构建慢但结果可靠。

### 6.1 Android

```bash
./gradlew :androidApp:compileDebugKotlin   # 只验证编译
./gradlew :androidApp:installDebug         # 编译 + 安装
adb logcat | grep -iE "CanvasGLView|Kuikly"   # 运行日志
```

**预期**：App 启动直接落在 `main` 页（白底居中蓝绿圆角按钮"进入画布"），点击后进入**整页纯红**。

**本仓库当前状态（重要）**：`:androidApp:compileDebugKotlin` 与 KSP 均**通过**，已确认生成的
`KuiklyCoreEntry` 含 `main` 与 `canvas`；但 `:androidApp:assembleDebug`（打 APK）在本机被一个
**与本功能无关的工具链问题**阻塞：

```
ERROR:D8: com.android.tools.r8.kotlin.H
> Failed to transform kotlin-stdlib-2.1.21.jar ...
> Failed to transform core-release-runtime.jar ...
> Failed to transform core-annotations-jvm-2.7.0-2.1.21.jar ...
> Error while dexing.
```

- 根因：AGP `7.4.2` 自带的 R8 无法解析 Kotlin `2.1` 的 metadata。
- 已排除：JDK 版本（JDK 11 同样失败）、dexing transform 路径
  （`-Pandroid.enableDexingArtifactTransform=false` 同样失败）。
- 结论：**构建环境版本组合问题，不在本次改动范围**，未改动任何构建配置。
- 若要跑通 APK：把 AGP 升到 8.1+（连带升级 R8），或用与 Kotlin 2.1 匹配的 R8 版本。

### 6.2 iOS

```bash
cd iosApp
pod install            # 首次或 shared 变更后需要
open iosApp.xcworkspace
plutil -lint iosApp.xcodeproj/project.pbxproj   # 快速校验工程文件没写坏
```

**预期**：首屏 `main` 页（白底 + "进入画布"按钮），点击后整页纯红。

**前置检查**：
- `CanvasGLView.h/.m` 在 `KuiklyExpand/Views` 分组可见，且 `CanvasGLView.m` 在
  `Build Phases → Compile Sources` 中（本次已直接改 `project.pbxproj` 完成）；
- 编译报找不到 `OpenKuiklyIOSRender` / `shared` 时先跑 `pod install`。

**日志排查**：Xcode 控制台过滤 `CanvasGLView`：
- 无输出 → 正常走 Metal 渲染；
- `create MTLDevice failed, fallback to solid background color`（拿不到 Metal 设备时的降级日志）。

Metal 失败路径（设备创建失败、`nextDrawable` 拿不到）均保持**纯色兜底**，页面仍是红色——正好可用来区分
"Metal 失败但功能没瞎" 与 "View 根本没渲染出来（白/黑屏）"。部署目标 iOS 14.1+，模拟器与真机均支持 Metal。

### 6.3 鸿蒙

仓库已提供一键脚本 `ohosApp/runOhosApp.sh`（ohpm install → hvigorw assembleHap → hdc 安装/启动），
也可以直接用 DevEco Studio 打开 `ohosApp`，选 entry 模块编译运行到模拟器/真机。

**前置：先构建 shared 的鸿蒙产物**

`ohosApp/entry/src/main/cpp/CMakeLists.txt` 以 `IMPORTED` 方式链接
`ohosApp/entry/libs/${OHOS_ARCH}/libshared.so`，而这个 so 由 `:shared` 的 `ohosArm64` 目标产出，
**不在 hvigor 构建链里**。所以改了 Kuikly 业务代码后必须手动重建并拷贝，否则跑的是旧 so：

```bash
./gradlew -c settings.ohos.gradle.kts :shared:linkReleaseSharedOhosArm64
# 产物：shared/build/bin/ohosArm64/releaseShared/libshared.so
cp shared/build/bin/ohosArm64/releaseShared/libshared.so ohosApp/entry/libs/arm64-v8a/
```

> 任务名如与上面不一致，用 `./gradlew -c settings.ohos.gradle.kts :shared:tasks --all` 搜 `link*OhosArm64`。
> 注意鸿蒙这条链用的是 `settings.ohos.gradle.kts` + `build.ohos.gradle.kts`，
> 其 Kotlin 版本与主工程不同（`2.0.21-KBA-010`），两条链各自独立。

**验证要点**

- 改了 native 源文件（`canvas_gl_render.cpp`）后建议 **Build → Clean Project** 再 Rebuild，
  避免增量缓存漏编；
- 日志过滤 `KuiklyRender KRCanvasGLView`（如 `setCanvasClearColor failed`）；
- 避免宽高设置过大（该问题在各端都存在，安卓下尤其容易 crash）。

### 6.4 H5

```bash
# 1) 先构建业务 JS 产物（生成 shared/build/outputs/kuikly/js/release/local/nativevue2.zip）
./gradlew :shared:packLocalJSBundleRelease

# 2) 再让 h5App 复制产物 + 资源，并把 index.html 的 js 引用改成生产路径
./gradlew :h5App:publishLocalJSBundle

# 3) 托管 h5App/build/distributions 目录
#    注意：项目自带的 npm run serve 托管的是 static_server/static（端口 8017），
#    并不是 distributions 目录。两种做法二选一：
cp -r h5App/build/distributions/* static_server/static/ && npm run serve
# 或者用任意静态服务器直接指向产物目录
npx --yes serve h5App/build/distributions
```

然后浏览器访问该服务的 `index.html`（`index.html` 里的 js 引用已被 `publishLocalJSBundle`
替换为生产路径 `page/nativevue2.js`）。

**预期**：首屏 `main` 页，点击按钮进入整页纯红。
可用 `?page_name=canvas` 直接打开画布页，跳过按钮（便于单独验证 View）。

**调试**：
- DevTools Console 过滤 `CanvasGLView`；WebGL 不可用时会打印
  `WebGL is unavailable, fallback to solid background`，此时是 CSS 纯色兜底，页面仍为红色。
- 想单独调试业务代码，可用 `./gradlew :h5App:jsBrowserDevelopmentRun` 起 dev server。

### 6.5 微信小程序

```bash
# 1) 业务 JS 产物
./gradlew :shared:packLocalJSBundleDebug

# 2) 构建小程序包并同步到 miniApp/dist
./gradlew :miniApp:jsMiniAppDevelopmentWebpack   # 或 :miniApp:jsMiniAppProductionWebpack
```

然后用**微信开发者工具**打开 `miniApp/dist` 目录。

**预期**：首屏 `main` 页，点击按钮进入整页纯红。

> 说明：小程序侧没有修改 `miniApp/dist/base.wxml`（4.5 的原因），
> `jsMiniApp*Webpack` 只同步 `dist/lib`（渲染库）与 `dist/business/nativevue2.js`（业务包）。

**运行限制与验证要点**：

| 项 | 要求 / 现象 |
| --- | --- |
| 基础库版本 | `type="webgl"` 需 **≥ 2.7.0** |
| GPU 加速 | 开发者工具**默认关闭**，需在"设置 → 通用设置"里开启「硬件加速」，否则可能拿不到 WebGL 上下文（此时走纯色兜底，仍是红色） |
| 真机调试 | **WebGL 不支持真机调试**，要验证真机请用「预览」扫码 |
| 平台支持 | 微信 Windows / Mac / 鸿蒙 OS 版均支持；Skyline 需最新 Nightly 工具 |
| 日志 | 过滤 `CanvasGLView`，能看到 `canvas node not found after retry` / `WebGL is unavailable` 等降级日志 |

---

## 七、已知问题与风险

| # | 问题 | 影响 | 处理 |
| --- | --- | --- | --- |
| 1 | AGP 7.4.2 + Kotlin 2.1 → D8 `com.android.tools.r8.kotlin.H` | Android **打 APK** 失败（编译通过） | 与本功能无关；需升 AGP 8.1+ |
| 2 | 小程序 webgl 不支持真机调试、工具默认关 GPU 加速 | 小程序验证需额外开配置 | 见 6.5 |
| 3 | `GLSurfaceView` / `XComponent(SURFACE)` 不支持圆角、半透明、层级叠加 | 只适合整页/独立区域铺满 | 本场景即整页铺满，符合预期 |
| 4 | 两端 Surface 均"异步就绪" | 属性可能早于 Surface 到达 | 已分别用 `RENDERMODE_WHEN_DIRTY` + 首帧回调和 native 颜色缓存处理 |
| 5 | 小程序 canvas 节点异步渲染 | 首次查询可能为空 | 已做 20×50ms 有界重试 + 纯色兜底 |
| 6 | OpenGL ES 在 iOS 已废弃（deprecated since iOS 12） | iOS 长期兼容性风险 | iOS 已改用 Metal（`CAMetalLayer` + `MTLDevice`）；Android / 鸿蒙 / H5 / 小程序仍用 GL 系技术，无此问题 |

---

## 八、附录

### 8.1 颜色解析约定

`#AARRGGBB` 为 Kuikly/Android 风格（**alpha 在前**），各端解析保持同一顺序：

| 长度 | 含义 |
| --- | --- |
| 3 | `#RGB`（H5/小程序支持，自动展开为 `#FFRRGGBB`） |
| 6 | `#RRGGBB`，alpha 补 1.0 |
| 8 | `#AARRGGBB` |

解析失败：Android 返回 `null` 保留原色；iOS 返回 `NO` 不改写；H5/小程序回落到默认红。

### 8.2 onReady 触发时机

| 平台 | 时机 |
| --- | --- |
| Android | `onDrawFrame` 中首次 `glClear` 后，`post` 回主线程 |
| iOS | `p_render` 中 Metal command buffer `presentDrawable` 提交后（或纯色兜底时） |
| 鸿蒙 | `XComponent.onLoad`（Surface 就绪）后 |
| H5 | 首次 `clear` 后（或 CSS 兜底时） |
| 小程序 | WebGL `clear` 后（或 CSS 兜底时） |

各端都保证**只回调一次**，且允许"GL 不可用但已降级为纯色"时也回调，便于业务侧统一做打点。

### 8.3 相关构建命令速查

```bash
# 共享层
./gradlew :shared:packLocalJSBundleRelease        # 业务 JS 包（H5/小程序用）
./gradlew :shared:packLocalJSBundleDebug
./gradlew :shared:packSplitJSBundleRelease        # 分包构建

# 各端
./gradlew :androidApp:compileDebugKotlin
./gradlew :androidApp:installDebug
./gradlew :h5App:publishLocalJSBundle
./gradlew :h5App:jsBrowserDevelopmentRun
./gradlew :miniApp:jsMiniAppDevelopmentWebpack
./gradlew :miniApp:jsMiniAppProductionWebpack
./gradlew -c settings.ohos.gradle.kts :shared:linkReleaseSharedOhosArm64

# Android（需先解决上面的 AGP/R8 问题）
./gradlew :androidApp:installDebug

# iOS
open iosApp/iosApp.xcodeproj   # 编译运行，首屏应为按钮，点击进入纯红页

# 鸿蒙：DevEco 打开 ohosApp，编译 entry 后运行

# H5
./gradlew :shared:packJsLocalAssets
./gradlew :h5App:publishLocalJSBundle

# 小程序
./gradlew :shared:packJsLocalAssets
./gradlew :miniApp:jsMiniAppProductionWebpack   # 产物落在 miniApp/dist，用微信开发者工具打开
```

---

## 九、统一绘制（Skia）实现清单

### 9.1 分层

```
shared/commonMain           CanvasScene（scene DSL）
        │  JSON（唯一绘制意图）
        ├──────────────┬───────────────────────────┐
        ▼              ▼                           ▼
  Android/iOS/鸿蒙   H5                      小程序
  cppCore/canvasCore CanvasKit（Skia WASM）  Canvas2D（同 ops 子集）
  （C++ / Skia）
        │
        ├─ scene.cpp     JSON -> ops（jsmn）
        ├─ painter.cpp   ops -> SkCanvas（与 Web 播放器逐条对齐）
        └─ canvas_core.cpp  raster 表面（GPU 后端预留）
```

### 9.2 各端接入点

| 端 | 文件 | 说明 |
| --- | --- | --- |
| 契约 | `cppCore/canvasCore/include/canvas_core.h` | 只暴露 C 类型：create/destroy/set_size/set_scene/set_clear_color/render_raster |
| 共享 | `shared/.../canvas/CanvasScene.kt` | `clear/rect/line` 的 DSL 与 JSON 编码 |
| Android | `androidApp/src/main/cpp/{CMakeLists.txt,skia_canvas_jni.cpp}`、`expandview/CanvasGLView.kt` | `Bitmap` 交出像素指针，Skia 直接画进去后 `drawBitmap` |
| iOS | `cppCore/canvasCore/canvas_core.podspec`、`iosApp/.../CanvasGLView.m` | 本地 pod 编译 C++ 并链接 vcpkg Skia；raster 结果包成 `CGImage` 显示 |
| 鸿蒙 | `ohosApp/entry/src/main/cpp/{CMakeLists.txt,canvas_gl_render.cpp,napi_init.cpp}`、`KRCanvasGLView.ets` | `XComponent` + EGL：Skia 光栅化到 RGBA，再上传纹理铺满 Surface |
| H5 | `canvasWeb/.../CanvasKitRenderer.kt`、`h5App/.../components/CanvasGLView.kt` | CanvasKit 的 WebGL surface，失败回退软件 surface / CSS 纯色 |
| 小程序 | `canvasWeb/.../WebScene.kt`、`miniApp/.../components/CanvasGLView.kt` | `type="2d"` + Canvas2D 播放同一份 ops |

### 9.3 构建前置

- Skia 产物由仓库根 `vcpkg/installed/<platform>/` 提供（**已 gitignore**）：
  - `android/arm64-android`、`ios/arm64-ios`、`ohos/arm64-ohos`：静态库 + 头文件
  - `wasm/canvaskit`：`canvaskit.js` + `canvaskit.wasm`
- Android 可通过 `-Pskia.android.dir=<dir>` 或 `SKIA_ANDROID_DIR` 覆盖默认路径
- iOS 需 `pod install`（`canvas_core` 本地 pod），Skia 静态库通过 `user_target_xcconfig` 链接；
  产物 `minos 26.5` 会触发部署目标告警，后续应以 `IOS_DEPLOYMENT_TARGET=14.1` 重新构建
- H5 的 `publishLocalJSBundle` 会把 `canvaskit.js/.wasm` 拷进 `build/distributions`，并把
  `build/dist/js/productionExecutable` 的 `index.html`/`h5App.js` 一并聚合到该目录

### 9.4 已知限制

| # | 限制 | 影响 |
| --- | --- | --- |
| 1 | iOS 只有 device slice（arm64/arm64e） | 模拟器无法运行，需真机或补 simulator 构建 |
| 2 | 鸿蒙 Skia 只提供 arm64 | 仅 arm64 设备/模拟器 |
| 3 | 小程序用 Canvas2D 而非 CanvasKit | `blur/gradient/path` 等后续 op 需补 Canvas2D 实现或另开 WASM 方案 |
| 4 | raster 为整帧重绘 | 动效场景需切 GPU 后端（Metal / EGL 直绘） |

### 9.5 绘制后端开关（raster / gpu / auto）

Kuikly 侧通过 `CanvasGLView { attr { backend(...) } }` 选择后端，五端同一套取值：

| 值 | 行为 |
| --- | --- |
| `CanvasBackend.RASTER` | 强制 CPU 光栅化 |
| `CanvasBackend.GPU` | 优先 GPU；attach 失败自动回退 raster |
| `CanvasBackend.AUTO`（默认） | 同 GPU，失败回退 |

```kotlin
CanvasGLView {
    attr {
        absolutePositionAllZero()
        size(ctx.pagerData.pageViewWidth, ctx.pagerData.pageViewHeight)
        scene(CanvasScene.clear(0xFFFF0000))
        backend(CanvasBackend.AUTO)
    }
}
```

> 刻意用字符串常量而不是 enum：Kotlin/JS 的 DCE 会丢掉未被直接引用的枚举项
> （实测 `CanvasBackend.RASTER` 被编译成 `AUTO`），常量在 JVM/Native/JS 三端行为一致。

各端实现：

| 端 | raster | gpu | 回退 |
| --- | --- | --- | --- |
| Android | `View` + `Bitmap` + `skc_render_raster` | `GLSurfaceView` + `skc_attach_gl` + `skc_render_gl(FBO 0)` | attach 失败 → 换回 raster 子视图 |
| iOS | `UIView` + `CGImage` | `CAEAGLLayer` + `skc_attach_eagl` + `skc_render_eagl` | attach/render 失败 → 隐藏 EAGL 层走 raster |
| 鸿蒙 | Skia 光栅化 + GL 纹理上传 | EGL 上下文内 `skc_render_gl(FBO 0)` | attach/render 失败 → 继续纹理路径 |
| H5 | CanvasKit `MakeSWCanvasSurface` | CanvasKit `MakeWebGLCanvasSurface` | WebGL 不可用 → 软件 surface → CSS 纯色 |
| 小程序 | Canvas2D | 暂不支持（无 Skia 运行时） | 打 warning，保持 Canvas2D |

C ABI 新增（`cppCore/canvasCore/include/canvas_core.h`）：

```c
typedef enum skc_backend { SKC_BACKEND_RASTER = 0, SKC_BACKEND_GPU = 1, SKC_BACKEND_AUTO = 2 } skc_backend;
void        skc_set_backend(skc_canvas*, skc_backend);
skc_backend skc_get_backend(const skc_canvas*);      /* 实际生效的后端 */
const char* skc_backend_name(const skc_canvas*);     /* "raster" / "gpu" */

bool skc_attach_gl(skc_canvas*);                     /* 调用线程需已 makeCurrent */
bool skc_render_gl(skc_canvas*, uint32_t fbo_id, int32_t w, int32_t h);
void skc_detach_gpu(skc_canvas*);

/* iOS：把 CAEAGLLayer 交给 core，EAGLContext/FBO/present 全部在 core 内完成 */
bool skc_attach_eagl(skc_canvas*, void* ca_eagl_layer);
bool skc_render_eagl(skc_canvas*, int32_t w, int32_t h);
```

GPU 实现细节：

- 原生三端共用 `src/gpu_surface_gl.cpp`（EGL）与 `src/gpu_surface_eagl.mm`（iOS）：
  `GrDirectContexts::MakeGL/MakeEGL/MakeIOS` + `GrBackendRenderTargets::MakeGL` +
  `SkSurfaces::WrapBackendRenderTarget`，每帧 `flushAndSubmit`。
- 静态画面只在场景/尺寸变化时重绘，不跑渲染循环。
- **iOS 的 Skia 构建没有 Metal 后端**（`libskia.a` 里只有 Ganesh GL/EAGL 符号），
  因此 iOS GPU 走 EAGL 而非 Metal；后续若重编 Skia 开启 `skia_use_metal`，可只替换
  `gpu_surface_eagl.mm` 为 Metal 实现，ABI 不变。
- iOS 26 SDK 把 `OpenGLES/ES3/gl3.h` 改名为 `OpenGLES/ES3/gl.h`，pod 内已按新头文件名引用。

验证结果（本机）：

| 端 | raster | gpu |
| --- | --- | --- |
| Android 模拟器 | `render backend=raster`，纯红 | `render backend=gpu`，纯红 |
| H5（headless Chrome） | canvas 2D context，纯红 | WebGL2，纯红 |
| iOS | `xcodebuild` 通过 | `xcodebuild` 通过（需真机运行） |
| 鸿蒙 | `assembleHap` 通过 | `assembleHap` 通过（含 `skc_attach_gl`/`skc_render_gl`） |
| 小程序 | 构建通过（Canvas2D） | 不支持，运行时 warning |

### 9.6 H5 构建顺序修正

`publishLocalJSBundle` 里原本在配置阶段就拷贝 `nativevue2.zip`，会拿到上一轮产物
（表现为「改了 commonMain 但页面行为不变」）。现已改为 `doFirst` 执行拷贝，
依赖任务先跑完再取产物。
