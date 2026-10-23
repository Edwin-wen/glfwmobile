# 为什么 Skia（CanvasKit）在 H5 可用，在小程序不可用

> 适用范围：`canvasWeb/`、`h5App/`、`miniApp/` 的画布实现。
> 相关：[CanvasGLView-实现与验证.md](../CanvasGLView-实现与验证.md)（9.5 绘制后端开关）、[README.md](README.md)（小程序构建与调试）。

---

## 一、结论

Skia 本身没问题——它能编译成 WebAssembly。小程序跑不了的是 **CanvasKit 这个「面向浏览器」的发布物**：

- 它的 JS glue 依赖 `window` / `document` / `fetch` / 标准 `WebAssembly` / `requestAnimationFrame`；
- 微信小程序是自研运行时：**没有 DOM/BOM**，只有 JS 引擎、原生组件（canvas 节点）和 `WXWebAssembly`。

所以问题不是「小程序没有 GPU」或「Skia 不支持小程序」，而是 **wasm 加载链路和画布绑定方式不兼容**。

---

## 二、运行时差异

| 维度 | H5（浏览器） | 微信小程序 | 影响 |
| --- | --- | --- | --- |
| 运行环境 | 完整浏览器：`window` / `document` / `navigator` | 自研 JS 运行时，无 DOM/BOM | CanvasKit 的环境探测与 DOM 入口全部失效 |
| 加载 wasm | `fetch` / `XMLHttpRequest`，可 streaming | 只能 `wx.getFileSystemManager().readFileSync` 读包内文件 | 需自定义加载器 |
| wasm API | 标准 `WebAssembly` | `WXWebAssembly`（基础库 2.13.0+ 全局、2.15.0+ Worker），API 形态不同 | 不能用 Emscripten 默认的 `instantiateStreaming` |
| 画布对象 | 真 `HTMLCanvasElement`，有 `.width/.height/.style` | 原生 canvas 节点，只能 `SelectorQuery.node()` 获取 | `MakeWebGLCanvasSurface(canvasEl)` 传不进去 |
| WebGL 获取 | `canvas.getContext('webgl2'/'webgl')` | `node.getContext('webgl')`，返回微信包装的 GL 对象 | 只能走 `MakeWebGLContext(gl)` + `MakeOnScreenGLSurface` |
| 包体 | 无硬限制 | 主包 ≤ 2MB（项目按 1.5MB 控制），分包总计 20MB | 7.8MB 的 `canvaskit.wasm` 必须进分包 |
| 调试 | DevTools 完整 | WebGL 不支持真机调试；开发者工具默认关 GPU 加速 | 验证成本高 |

---

## 三、CanvasKit 启动链路的 4 个卡点

```
CanvasKitInit({ locateFile })
        │
        ├─ ① Emscripten 环境探测
        │     ENVIRONMENT_IS_WEB / WORKER / NODE 全不成立（没有 window / process）
        │
        ├─ ② 加载 wasm
        │     fetch / XHR 不存在  ← 直接失败
        │
        ├─ ③ 运行时 shim
        │     document.createElement('canvas') / requestAnimationFrame / Image 缺失
        │
        └─ ④ 绑定画布
              MakeWebGLCanvasSurface(HTMLCanvasElement) 需要 DOM 节点
              小程序只有原生 canvas 节点
```

这四步里任何一步都足以让 `CanvasKitInit` 失败。所以「Skia 能不能跑」和「CanvasKit 能不能直接拿来用」是两个问题。

---

## 四、本仓库的具体证据

### H5：整条链路都是浏览器原生能力

- [h5App/src/jsMain/resources/index.html](../../h5App/src/jsMain/resources/index.html) 用 `<script src="./canvaskit.js">` 加载 glue；
- [CanvasKitRenderer.kt](../../canvasWeb/src/jsMain/kotlin/com/edwin/glfwmobile/canvas/CanvasKitRenderer.kt) 里 `locateFile` 返回 `./canvaskit.wasm`（走 `fetch`），surface 用 `MakeWebGLCanvasSurface(canvas)`；
- 实测：canvas 的上下文是 `webgl2`，纯红渲染正常（见 9.5 验证表）。

### 小程序：画布不是 DOM 节点

- [MiniCanvasGLViewElement.kt](../../miniApp/src/jsMain/kotlin/dom/MiniCanvasGLViewElement.kt)：`createSelectorQuery().select('#id').node()` 拿原生节点，模板 `type="2d"`；
- [miniApp CanvasGLView.kt](../../miniApp/src/jsMain/kotlin/components/CanvasGLView.kt)：用 Canvas2D 播放同一份 scene ops；
- `backend(GPU)` 目前只打 warning：`gpu backend is not available on miniApp, keep Canvas2D`。

### 体积数字（本仓库实测）

| 产物 | 大小 |
| --- | --- |
| `canvaskit.js` | 132,339 B（约 129 KB） |
| `canvaskit.wasm` | 7,814,339 B（约 7.45 MB） |
| `miniApp/dist` 整包 | 约 1.5 MB（`lib/miniApp.js` 760,842 B + `business/nativevue2.js` 678,326 B） |

也就是说：即使只是把官方 CanvasKit 放进主包，也会直接超过 2MB 上限，必须分包 + 懒加载。

---

## 五、如果一定要在小程序用 Skia

按依赖顺序拆成四步，每步都是独立的 go/no-go：

### 1. 自建精简 wasm（决定体积上限）

不直接用官方 `canvaskit.wasm`，而是用 Emscripten 只编：

- Skia core + Ganesh GL（`skia_use_gl=true`）
- 我们的 `cppCore/canvasCore`（scene 解析 + painter）
- 关掉 codec / 字体 / effects / svg / skottie

目标体积 2–3MB（gzip 后更小），放分包。C ABI 可以完全复用 `canvas_core.h`。

### 2. 自定义加载器（决定能否启动）

- `wx.getFileSystemManager().readFileSync('.../canvas_core.wasm')` 拿 `ArrayBuffer`；
- `WXWebAssembly.instantiate(path, imports)` 或 Emscripten 的 `wasmBinary` / `instantiateWasm`；
- 不依赖 `fetch` / `document`，不引入 CanvasKit 的浏览器 glue。

### 3. 绑定 canvas 节点（决定能否画出来）

- `SelectorQuery.node()` 拿节点 → `node.getContext('webgl')`；
- 用 `MakeWebGLContext(gl)` + `MakeOnScreenGLSurface`（而不是 `MakeWebGLCanvasSurface`）；
- 尺寸用 `node.width/height` 与 `display.pixelRatio` 控制。

### 4. 分包 + 降级（决定可发布）

- wasm 与 glue 放分包，首次进入画布页时懒加载；
- 加载/初始化失败 → 回退现有 Canvas2D 播放器（已实现）；
- 真机验证只能「预览」扫码（WebGL 不支持真机调试）。

### 风险清单

| 风险 | 说明 |
| --- | --- |
| `WXWebAssembly` 兼容性 | 基础库 2.13.0+；不同端（iOS/Android/PC）实现细节需逐端验证 |
| 包体 | 主包 2MB、分包 20MB；wasm 压缩/裁剪与加载耗时都要算进首屏 |
| GL 包装差异 | 微信的 WebGL 对象与标准实现存在差异，Skia Ganesh 的扩展探测可能失败 |
| 调试成本 | 真机无法断点，只能靠日志 + 截图 |

---

## 六、建议

- **当前保持 Canvas2D**：现有 op 集合只有 `clear/rect/line`，Canvas2D 完全够用，且不增加包体。
- **触发投入的条件**：需要 `path` / `gradient` / `blur` / `text` 等 Skia 独有能力，或要求与原生端像素级一致。
- **投入前先做第 1 步的 spike**：能否把精简 wasm 压到 3MB 以内、能否在真机上 `WXWebAssembly.instantiate` 成功。这两件事任一失败，后面的工作都没有意义。

---

## 七、参考

- 微信官方 WXWebAssembly 文档：<https://developers.weixin.qq.com/miniprogram/dev/framework/performance/wasm.html>
- CanvasKit（Skia WASM）：<https://skia.org/docs/user/modules/canvaskit/>
- 项目内：
  - [canvasWeb/CanvasKitRenderer.kt](../../canvasWeb/src/jsMain/kotlin/com/edwin/glfwmobile/canvas/CanvasKitRenderer.kt)
  - [miniApp/MiniCanvasGLViewElement.kt](../../miniApp/src/jsMain/kotlin/dom/MiniCanvasGLViewElement.kt)
  - [docs/CanvasGLView-实现与验证.md](../CanvasGLView-实现与验证.md) 9.5 节