# H5 端（h5App）构建与调试指南

> 适用范围：`h5App/`（Kuikly Web 宿主）。
> 版本基线：Kotlin `2.1.21`、Kuikly `2.7.0-2.1.21`、webpack `5.94.0`。

---

## 一、三层产物与职责

```
h5App/
├── src/jsMain/kotlin/          # Web 宿主代码（Main.kt / Delegator / Module / utils）
├── src/jsMain/resources/       # index.html 模板 + （运行时无）静态资源
├── webpack.config.d/           # webpack 自定义配置（源码，需入库）
└── build/distributions/        # 发布产物（publishLocalJSBundle 产出）

shared/                         # 业务代码，产物 nativevue2.js
static_server/static/           # 业务 bundle 的静态发布目录（生成物，勿入库）
```

运行期两类 JS 互相独立：

| 文件 | 来源 | 作用 |
| --- | --- | --- |
| `h5App.js` | `:h5App:jsBrowserDevelopmentWebpack` | 渲染引擎 + 宿主代码（含 Kuikly render web base/h5、stdlib） |
| `nativevue2.js` | `:shared:jsBrowserDevelopmentWebpack` | 业务页面代码，被 `h5App.js` 通过 `global.com` / `callKotlinMethod` 调用 |

`index.html` 里两个 `<script>` 的顺序不能颠倒：**业务 bundle 在前，`h5App.js` 在后**。

---

## 二、标准启动流程

```bash
npm install                                   # 首次；缺少会报 Cannot find module 'koa'
./gradlew :h5App:jsBrowserDevelopmentRun -t   # 8080：编译 + dev server（自动同步业务 bundle）
```

访问：

| 设备 | 地址 | 说明 |
| --- | --- | --- |
| 电脑 | http://localhost:8080 | |
| 手机（同 Wi-Fi） | http://<电脑局域网IP>:8080 | 无需代理，dev server 监听 `*:8080` |

切换页面用 query 参数：`http://localhost:8080/?page_name=router`，缺省是 `main`
（见 `h5App/src/jsMain/kotlin/Main.kt`）。

---

## 三、业务 bundle 如何被浏览器拿到

dev server 会发布三个静态来源（启动日志里可见）：

```
[kotlin], h5App/build/processedResources/js/main, static_server/static
```

```
./gradlew :h5App:jsBrowserDevelopmentRun
  ├─ :shared:jsBrowserDevelopmentWebpack        → shared/build/dist/js/developmentExecutable/nativevue2.js
  ├─ :h5App:syncBusinessBundleToStatic          → static_server/static/nativevue2.js
  └─ webpack-dev-server :8080
        ├─ /                → index.html
        ├─ /h5App.js        → 宿主 bundle
        └─ /nativevue2.js   → static_server/static（由 webpack.config.d/devserver.js 追加）
```

`index.html` 因此只需写相对路径，任何 host 访问都能自洽：

```html
<script src="./nativevue2.js"></script>
```

---

## 四、端口的历史包袱（重要）

模板里原本有两套服务，互相并不配套：

| 端口 | 谁在监听 | 用途 | 现状 |
| --- | --- | --- | --- |
| 8080 | webpack-dev-server | H5 调试主服务 | 使用中 |
| 8017 | `static_server/serve`（koa-static） | 纯静态文件服务，根目录 `static_server/static` | 仅真机 App 调试需要 |
| 8083 | whistle（`w2 start -p 8083`） | 真机请求拦截代理 | 仅真机 App 调试需要 |

原 `index.html` 写的是 `http://127.0.0.1:8083/nativevue2.js`，但这套配置跑不通：

- `.whistle.js` 只映射 `nv_js` / `nv_so` 两条规则，`/nativevue2.js` 命中不了 → whistle 直接 404
- 手机上的 `127.0.0.1` 指向手机自己，除非设置 HTTP 代理（代理生效时 `127.0.0.1` 才由代理机解析）

所以 H5 直接改为同源相对路径，彻底不依赖 8017/8083。

> 真机 App 的 `nv_js` 热更新仍走 whistle，与 H5 互不影响。

---

## 五、改业务代码后要做什么

| 改动范围 | 需要执行 |
| --- | --- |
| `shared/` 业务 Kotlin | `./gradlew :shared:jsBrowserDevelopmentWebpack`，然后**手动刷新浏览器** |
| `h5App/src/` 宿主 Kotlin | `-t` 模式下自动重编；否则重启 `:h5App:jsBrowserDevelopmentRun` |
| `h5App/src/jsMain/resources/index.html` | `-t` 模式下自动重拷；否则重启 dev server |
| `h5App/webpack.config.d/*.js` | 必须重启 dev server，配置只在启动时读取 |

业务 bundle 是独立 `<script>`，没有 HMR 通道：shared 重编后浏览器不会自动更新，
刷新页面即可（不要用强缓存刷新，避免拿到旧 bundle）。

---

## 六、发布打包

```bash
./gradlew :shared:packLocalJsBundleRelease    # 生成 shared/build/outputs/kuikly/js/release/local/nativevue2.zip
./gradlew :h5App:publishLocalJSBundle         # 解压 → page/nativevue2.js，并改写 index.html
```

`generateLocalHtml()` 用字符串替换把开发期占位串换掉：

```kotlin
val placeText = "./nativevue2.js"
// index.html: <script src="./nativevue2.js"> → <script src="page/nativevue2.js">
```

**改动 `index.html` 里的 script 路径时，必须同步修改 `h5App/build.gradle.kts` 中
`generateLocalHtml` / `generateSplitHtml` 的 `placeText`**，否则发布产物仍指向开发地址。

---

## 七、踩坑速查

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `npm run serve` 报 `Cannot find module 'koa'` | 未装依赖 | `npm install` |
| 页面白屏、`h5App.js` 已加载 | 业务 bundle 404，`global.com` 缺失 | 检查 `/nativevue2.js` 是否 200 |
| `8083/nativevue2.js` 返回 `Cannot GET` | whistle 规则只覆盖 `nv_js`/`nv_so` | 改用同源相对路径（当前方案） |
| 手机访问白屏、电脑正常 | html 里写了 `127.0.0.1` 绝对地址 | 用相对路径，或把手机 HTTP 代理指向电脑 |
| Gradle 报 “uses this output of task … without declaring dependency” | 往 `build/processedResources/js/main` 写文件，撞上 KGP 的任务输出 | 改用 `devServer.static` 追加目录 |
| `webpack.config.d/*.js` 里 `__dirname` 指向 `build/js/packages/<pkg>` | 配置文件被复制到构建目录执行 | 向上查找 `settings.gradle.kts` 定位仓库根 |
| `webpack.config.d/*.js` 提交不进 git | 全局 `*.js` 忽略规则 | `.gitignore` 里 `!h5App/webpack.config.d/**/*.js` |
| 控制台 `Cannot use 'import.meta' outside a module` | 模板引入的 `libpag@latest` CDN 脚本已改用 ESM | 与业务无关，可删除该 `<script>` |

---

## 八、文件职责

| 位置 | 负责内容 | 谁维护 |
| --- | --- | --- |
| `shared/src/commonMain` | 业务页面、`@Page` 声明 | 业务代码 |
| `h5App/src/jsMain/kotlin` | 宿主入口 `Main.kt`、Delegator、自定义 Module/View | 宿主适配 |
| `h5App/src/jsMain/resources/index.html` | 页面容器、脚本加载顺序与路径 | 手工维护 |
| `h5App/webpack.config.d/` | dev server 静态目录等 webpack 扩展 | 手工维护（需入库） |
| `h5App/build/`、`static_server/static/` | 编译与同步产物 | 构建生成，勿入库 |
| `static_server/serve/` | 真机调试用的 koa + whistle 启动脚本 | 仅 App 调试相关 |