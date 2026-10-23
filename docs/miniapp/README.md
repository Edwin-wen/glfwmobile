# 微信小程序端（miniApp）构建与调试注意事项

> 适用范围：`miniApp/`（Kuikly 小程序宿主），产物目录 `miniApp/dist/`，用微信开发者工具直接导入该目录编译。
> 版本基线：Kuikly `2.7.0-2.1.21`，基础库 `3.6.1`。

---

## 一、产物结构：三层职责

```
miniApp/dist/
├── app.js / app.json / app.wxss      # 手写维护，构建不会生成也不会覆盖
├── base.wxml  comp.*  custom-wrapper.*  utils.wxs
│                                     # 渲染模板层，手写维护
├── lib/miniApp.js                    # Kuikly Web 渲染运行时（gradle 生成）
├── business/nativevue2.js            # 业务代码（shared 模块产物，gradle 生成）
└── pages/<pageName>/index.{js,json,wxml}
                                      # 每个 Kuikly 页面一层的“页面壳”，手写维护
```

启动链路：

```
app.js
 ├─ require('./business/nativevue2.js')   → global.com / global.callKotlinMethod
 ├─ require('./lib/miniApp.js')           → render.initApp()
 └─ pages/<p>/index.js
      └─ render.renderView({ pageName }) → 渲染库内部执行 Page({...})
```

关键点：渲染库的 `initMiniPage()` 内部就是 `Page({ data: { root: { cn: [] } }, ... })`，
所以**页面壳的 js 必须在模块顶层调用 `renderView`**，不能只写 `Page({})`。事件绑定
（模板里的 `bindtap="eh"`）由渲染库注入 `global.eventHandler`，不需要手写。

---

## 二、新增一个 Kuikly 页面的固定动作

`shared` 里每加一个 `@Page("xxx")`，小程序侧必须同步三处，缺一处就是“白屏/点了没反应”：

1. `dist/pages/xxx/index.js` —— `pageName` 必须与 `@Page` 名字**完全一致**
2. `dist/pages/xxx/index.wxml` + `index.json`（`usingComponents` 指回 `../../comp`、`../../custom-wrapper`）
3. `dist/app.json` 的 `pages` 数组注册 `"pages/xxx/index"`

```js
// dist/pages/canvas/index.js
var render = require('../../lib/miniApp.js')

render.renderView({
    pageName: "canvas"     // ← 与 @Page("canvas") 对应
})
```

```json
// dist/pages/canvas/index.json
{
  "navigationBarTitleText": "CanvasPage",
  "disableScroll": true,
  "usingComponents": {
    "custom-wrapper": "../../custom-wrapper",
    "comp": "../../comp"
  }
}
```

```xml
<!-- dist/pages/canvas/index.wxml -->
<import src="../../base.wxml"/>
<template is="kuikly_tmpl" data="{{root:root}}" />
```

> 当前 `shared` 中已声明但**尚未**建壳的页面：`router`、`image_adapter`。
> 从路由页输入这些 pageName 跳转会失败，需要时按上面三步补。

---

## 三、页面路由机制

`RouterModule.openPage(pageName, pageData)` 在小程序端的实际行为：

```js
// lib/miniApp.js 中的平台实现
open(url) {                                  // url 形如 "?page_name=canvas"
  const parts = split(url, "?");
  if (parts.size > 1) {
    const query = parts.get(1);
    wx.navigateTo({ url: `/pages/${getParam("page_name", query)}/index?${query}` });
  }
}
```

结论：

- 目标路径恒为 `/pages/<page_name>/index` —— **目录名 = pageName**，不存在自定义路径
- 拼接后的 query 里带 `page_name`，页面壳即使写错 `pageName` 也能被 query 覆盖，但仍建议显式写对
- `navigateTo` 失败（页面未注册 / 未建壳）时**只打 console，不弹错误**，表现就是“点了没反应”
  排查顺序：`app.json` 有没有注册 → `pages/<name>/` 三件套在不在 → `@Page` 名字对不对
- 页面返回走 `RouterModule.closePage()` → `wx.navigateBack()`

---

## 四、包体积：必须先压缩，否则编译不通过

微信限制：主包 2M（建议 1.5M），而 Kuikly 的 webpack 产物默认未压缩。

| 文件 | 未压缩 | terser 压缩后 |
| --- | --- | --- |
| `lib/miniApp.js` | 2050K | 714K |
| `business/nativevue2.js` | 1841K | 650K |
| `dist` 整包 | 3.9M | **1.44M** |

压缩脚本：`miniApp/tools/minify-dist.sh`（terser `--compress --mangle --comments false`，只处理
`lib/*.js`、`business/*.js`，同时清掉 `.map`）。属性名不混淆，UMD 导出面不变，可安全用于运行。

已接入构建：`minifyMiniAppDist` 作为 `syncRender*ToDist` 与 `jsMiniApp*Webpack` 的 finalizer，
并 `mustRunAfter` 两个 sync 任务以保证「先拷贝、后压缩」。也可单独执行：

```bash
./gradlew :miniApp:minifyMiniAppDist     # 手工改过 dist 下的 js 后使用
```

体积余量不足时（业务代码继续增长）的下一步手段，按优先级：

1. 发布用 production 构建（产物小于 development）
2. 把 `business/nativevue2.js` 拆到分包
   —— **不能把 `lib/` 挪进分包**：`pages/*/index.js` 是同步 `require` 渲染库的

---

## 五、重新生成 js 产物的命令

`lib/` 与 `business/` 来自两个模块，**必须分两次执行**：

```bash
# 开发调试
./gradlew :shared:jsBrowserDevelopmentWebpack
./gradlew :miniApp:jsMiniAppDevelopmentWebpack

# 发布
./gradlew :shared:jsBrowserProductionWebpack
./gradlew :miniApp:jsMiniAppProductionWebpack
```

| 改动范围 | 需要执行 |
| --- | --- |
| `shared/` 业务 Kotlin 代码 | 两条都执行 |
| `miniApp/src/`（Main.kt、components、module） | 只要第二条 |
| `dist` 静态文件（app.json、wxml、页面壳） | 都不用，开发者工具里重新编译即可 |

**为什么要分两条**：`miniApp/build.gradle.kts` 里的 `copyLocalJSBundle()` 写在
`tasks.register { }` 的配置块中，在**配置阶段**（任务图计算时）就执行了拷贝，早于 `:shared`
的编译。若写进同一条命令，拷进 `dist/business` 的会是上一次构建的旧文件。

---

## 六、app.json 必选项：组件按需注入

```json
{
  "pages": ["pages/index/index", "pages/canvas/index"],
  "lazyCodeLoading": "requiredComponents"
}
```

缺少 `lazyCodeLoading` 时开发者工具会报「启用组件按需注入」并阻断编译。
该配置只按 `usingComponents` 注入，不影响 Kuikly 的模板渲染。

---

## 七、常见现象速查

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 编译报「代码包…超过 1.5 M」 | `dist` 内 js 未压缩 | 跑构建（会自动压缩）或 `:miniApp:minifyMiniAppDist` |
| 编译报「启用组件按需注入」 | `app.json` 缺 `lazyCodeLoading` | 按第六节补上 |
| 点按钮跳转无反应 | 目标页面未注册/未建壳 | 按第二节补三件套 + `app.json` |
| 页面白屏、无任何报错 | `index.js` 没在顶层调 `renderView`，或 `pageName` 与 `@Page` 不一致 | 对照第二节检查 |
| 改了代码但小程序表现没变 | `dist/business` 是旧产物 | 按第五节先跑 `:shared:...Webpack` 再跑 `:miniApp:...Webpack` |
| 清缓存后仍报旧错 | 开发者工具缓存了 `app.json` | 「工具 → 清除缓存 → 全部清除」后重新编译 |

---

## 八、责任划分

| 位置 | 负责内容 | 谁维护 |
| --- | --- | --- |
| `shared/` | 业务页面、`@Page` 声明 | 业务代码 |
| `miniApp/src/` | 小程序侧 View/Module 实现、`renderView` 入口 | 宿主适配 |
| `miniApp/dist/`（除 lib、business） | 小程序壳、页面注册、模板 | 手工维护 |
| `miniApp/dist/lib`、`dist/business` | 渲染运行时、业务产物 | gradle 生成 |
| `miniApp/tools/minify-dist.sh` | 产物压缩 | 构建后处理 |

---

## 九、延伸阅读

- [Skia（CanvasKit）为什么在 H5 可用、在小程序不可用](Skia-与H5的运行时差异.md)
  —— 运行时差异、CanvasKit 的 4 个启动卡点、体积数据，以及要接入时的四步方案。
