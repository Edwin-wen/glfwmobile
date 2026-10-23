# vcpkg（C++ 三方库 / Skia 产物）

这个目录不是 vcpkg 工具的源码，而是**项目侧的 vcpkg 工作区**：

```
vcpkg/
├── vcpkg.json              # manifest：唯一依赖清单（skia + freetype/gl/harfbuzz/icu）
├── triplets/               # 三端目标（arm64）
│   ├── arm64-android.cmake
│   ├── arm64-ios.cmake
│   └── arm64-ohos.cmake
├── toolchains/ohos.cmake   # 把 OHOS SDK 官方 toolchain 接到 vcpkg
├── ports/skia/             # overlay port：仅鸿蒙使用（上游 skia 没有 ohos target）
├── wasm/                   # CanvasKit（H5/小程序）构建配置
│   ├── DEPS.canvaskit      # 只含 CanvasKit 需要的 DEPS
│   ├── canvaskit.args.gn   # release / WebGL / CPU fallback
│   ├── skia/               # 独立源码树（不入库）
│   └── out/                # GN 构建目录（不入库）
├── scripts/                # bootstrap / env / build / build-canvaskit
├── installed/              # 产物（不入库）
└── tool/                   # 可选：项目内 vcpkg 工具（不入库）
```

## 原生三端映射

| 目标 | triplet | 工具链 | 产物 |
| --- | --- | --- | --- |
| Android arm64-v8a | `arm64-android` | NDK 27.1，API 28 | `installed/android/arm64-android` |
| iOS arm64 | `arm64-ios` | Xcode / iphoneos | `installed/ios/arm64-ios` |
| 鸿蒙 arm64-v8a | `arm64-ohos` | DevEco OpenHarmony native SDK | `installed/ohos/arm64-ohos` |

每个产物目录都包含 `include/skia/` 与 `lib/*.a`（Skia 及其静态依赖）。这正是
`cppCore/canvasCore/CMakeLists.txt` 需要的 `SKIA_DIR` 布局。

不同平台使用独立 install root：manifest 模式会移除当前依赖图之外的包，
三端共用同一个 root 会互相删除。

## 用法

```bash
# 1. 工具：默认复用 ~/ENV/vcpkg；也可在项目内克隆固定版本
vcpkg/scripts/bootstrap.sh

# 2. 构建（消费 lib/ 下的 release 产物；debug/ 也会生成）
vcpkg/scripts/build.sh android
vcpkg/scripts/build.sh ios
vcpkg/scripts/build.sh ohos
# 或一次全部
vcpkg/scripts/build.sh all
# 可选：Apple Silicon 模拟器
vcpkg/scripts/build.sh ios-sim
# H5 / 微信小程序：CanvasKit（wasm）
vcpkg/scripts/build.sh wasm
```

可覆盖的环境变量：`VCPKG_ROOT`、`ANDROID_SDK`、`ANDROID_NDK_HOME`、
`OHOS_SDK_NATIVE`。

## Web / 小程序（wasm / CanvasKit）

`vcpkg/scripts/build.sh wasm` 产出的是 **CanvasKit**，不是 wasm 静态库：

| 文件 | 大小 | 用途 |
| --- | --- | --- |
| `canvaskit.js` | 129 KB | Emscripten 生成的 JS loader（UMD） |
| `canvaskit.wasm` | 7.5 MB（gzip 2.99 MB / brotli 2.33 MB） | Skia m134 本体 |
| `canvaskit.d.ts` | 168 KB | TypeScript 类型 |

产物目录：`installed/wasm/canvaskit/`，附 `build-info.txt`（commit / emcc 版本 /
sha256）。

为什么不用 vcpkg 的 `wasm32-emscripten`：skia 端口只能产出 wasm 静态库，
H5/小程序真正消费的是 `CanvasKitInit` + embind 的 JS 绑定；官方 GN 的
`canvaskit.js` 才是可直接接入的产物。这条链路复用同一份 Skia 134 源码，
但不经过 vcpkg 的 skia 端口。

构建参数见 `wasm/canvaskit.args.gn`：release、WebGL/Ganesh + CPU raster
fallback、paragraph/fonts/pathops/skottie，关闭 tracing/DNG/Graphite/WebGPU。
Emscripten 固定 3.1.44（Skia 134 的 `bin/activate-emsdk`）。首次构建会拉取
CanvasKit 需要的 DEPS 并安装 emsdk（约几百 MB），之后增量构建约几分钟。

接入提示：

- H5：`CanvasKitInit({ locateFile: f => '/canvaskit/' + f })`；wasm 用
  `application/wasm` 响应，才能走 `instantiateStreaming`。
- 小程序：用 `WXWebAssembly.instantiate`，通过 `wx.getFileSystemManager()`
  把 wasm 读成 ArrayBuffer，再传给 `CanvasKitInit({ instantiateWasm, locateFile })`；
  wasm 放分包（brotli 后 2.33 MB，主包 1.5 MB 放不下）。
- `MakeWebGLCanvasSurface` 与 `MakeSWCanvasSurface` 都已包含，GPU 不可用时
  可退 CPU raster。
- `canvaskit.d.ts` 顶部引用了 `@webgpu/types`，TypeScript 工程需要把它加到
  devDependencies（即使当前构建未启用 WebGPU）。

## 依赖与裁剪

`vcpkg.json` 用 `default-features: false` 关掉了 skia 的默认特性，只保留：

```
freetype  gl  harfbuzz  icu
```

`expat / zlib / libpng / libjpeg-turbo / libwebp` 是 skia 端口本身的无条件依赖，
无法通过 feature 关闭。DNG 在 Android/iOS 上沿用端口默认（会编译 dng_sdk）；
鸿蒙 overlay 里显式 `skia_use_dng_sdk=false`，因为 musl 下 `int64_t` 与
`__builtin_smulll_overflow` 的参数类型不兼容。`pdf / graphite / vulkan / dawn`
均未启用。Skia 固定为 vcpkg 的 `134`（对应提交 `5300a2a`），三端版本一致。

## 鸿蒙为什么有 overlay port

上游 Skia 的 GN 只认识 `android/ios/linux/win/wasm`，没有 `ohos` target，也没有
`SkFontMgr_ohos`。因此鸿蒙走：

```
arm64-ohos triplet
  ├─ VCPKG_CMAKE_SYSTEM_NAME=Android  # 只为触发 vcpkg/meson 的 cross 分支
  └─ chainload OHOS SDK toolchain     # 实际 CMAKE_SYSTEM_NAME=OHOS、musl、aarch64-linux-ohos
        │
        ▼
ports/skia：target_os="linux"
  ├─ cc/cxx = OHOS clang + --target=aarch64-linux-ohos --sysroot=<sdk>/sysroot
  ├─ ar     = <sdk>/llvm/bin/llvm-ar
  ├─ skia_use_egl=true
  └─ skia_use_dng_sdk=false          # musl 下 int64_t 与 __builtin_smulll_overflow 不兼容
```

两个必要的 vcpkg 适配：

- `VCPKG_FIXUP_ELF_RPATH=OFF`：静态库产物不需要在 macOS 上改 ELF rpath，
  也避免额外安装 `patchelf`。
- `VCPKG_MAKE_BUILD_TRIPLET`：ICU 用 autotools，必须显式给出
  `--host/--build`，否则 configure 会尝试运行目标平台可执行文件。
  iOS 用 `--host=aarch64-apple-darwin --build=x86_64-apple-darwin`，
  鸿蒙用 `--host=aarch64-linux-gnu --build=aarch64-apple-darwin`。

已知限制：鸿蒙产物使用 Skia 的 Linux 路径，`SkFontMgr::Factory()` 不会自动读取
鸿蒙系统字体；文字渲染需要由业务侧传入字体文件（`SkTypeface::MakeFromFile/Data`
+ `skia_enable_fontmgr_custom_empty`），后续可换成 OpenHarmony 的 skia fork 补
`SkFontMgr_ohos`。图形/图片/路径渲染不受影响。DNG 解码未启用。

## 消费方式

CMake 只认一个目录：

```cmake
-DSKIA_DIR=/Users/edwin/github/glfwmobile/vcpkg/installed/android/arm64-android
```

Android 也可以继续用 Gradle 属性覆盖：

```bash
./gradlew :androidApp:assembleDebug \
  -Pskia.android.dir="$PWD/vcpkg/installed/android/arm64-android"
```

iOS / 鸿蒙分别把 `SKIA_DIR` 指向 `installed/ios/arm64-ios`、
`installed/ohos/arm64-ohos`。

## 不入库

`installed/`、`tool/`、vcpkg 的 `buildtrees/packages/downloads`，以及
`wasm/skia`、`wasm/out` 都由 `.gitignore` 排除。二进制产物通过本机构建或
内部制品仓分发；`wasm/DEPS.canvaskit` 与 `wasm/canvaskit.args.gn` 入库，
保证 CanvasKit 构建可复现。