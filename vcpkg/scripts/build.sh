#!/usr/bin/env bash
# 构建 Skia 产物。
#
#   ./scripts/build.sh all       Android + iOS 真机 + 鸿蒙
#   ./scripts/build.sh android
#   ./scripts/build.sh ios
#   ./scripts/build.sh ios-sim   可选：Apple Silicon 模拟器
#   ./scripts/build.sh ohos
#   ./scripts/build.sh wasm      CanvasKit（H5 / 微信小程序）
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

target="${1:-all}"
if [[ "$target" == "wasm" ]]; then
    exec "$SCRIPT_DIR/build-canvaskit.sh"
fi

source "$SCRIPT_DIR/env.sh"

case "$target" in
    android) triplets=(arm64-android) ;;
    ios)     triplets=(arm64-ios) ;;
    ios-sim) triplets=(arm64-ios-simulator) ;;
    ohos)    triplets=(arm64-ohos) ;;
    all)     triplets=(arm64-android arm64-ios arm64-ohos) ;;
    *)
        echo "用法：$0 [all|android|ios|ios-sim|ohos]" >&2
        exit 2
        ;;
esac

# 目标只消费 lib/（release）。当前 vcpkg 版本即使设置 release-only
# 仍会同时产出 debug/，不影响消费。
export VCPKG_BUILD_TYPE=release

# 按目标检查工具链，避免只构建 iOS/鸿蒙时被无关 SDK 缺失挡住。
require_file "$VCPKG"
case "$target" in
    android)        need_android=1 ;;
    ios|ios-sim)    need_xcode=1 ;;
    ohos)           need_ohos=1 ;;
    all)            need_android=1; need_xcode=1; need_ohos=1 ;;
esac
if [[ -n "${need_android:-}" ]]; then
    require_file "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
    require_file "$ANDROID_CMAKE_BIN/cmake"
fi
if [[ -n "${need_xcode:-}" ]]; then
    command -v xcodebuild >/dev/null || { echo "缺少 Xcode：xcodebuild 不可用" >&2; exit 1; }
fi
if [[ -n "${need_ohos:-}" ]]; then
    require_file "$OHOS_SDK_NATIVE/build/cmake/ohos.toolchain.cmake"
    require_file "$OHOS_CMAKE_BIN/cmake"
fi

for triplet in "${triplets[@]}"; do
    # manifest 模式会移除 install root 里不属于当前依赖图的包，
    # 因此每个平台必须使用独立的 install root。
    case "$triplet" in
        arm64-android)               platform=android ;;
        arm64-ios|arm64-ios-simulator) platform=ios ;;
        arm64-ohos)                  platform=ohos ;;
    esac
    platform_root="$INSTALL_ROOT/$platform"

    echo "==> $triplet  ($platform_root)"

    args=(
        install
        --x-manifest-root="$VCPKG_WORKSPACE"
        --x-install-root="$platform_root"
        --triplet="$triplet"
        --host-triplet=arm64-osx
        --overlay-triplets="$VCPKG_WORKSPACE/triplets"
    )

    if [[ "$triplet" == "arm64-ohos" ]]; then
        # 只有鸿蒙需要 overlay skia 端口（补 target_os/编译器/ar）。
        args+=(--overlay-ports="$VCPKG_WORKSPACE/ports")
        export PATH="$OHOS_CMAKE_BIN:$PATH"
    elif [[ -x "$ANDROID_CMAKE_BIN/cmake" ]]; then
        # 与已有 Android 产物 ABI 对齐；iOS 也优先用同一 CMake。
        export PATH="$ANDROID_CMAKE_BIN:$PATH"
    fi

    "$VCPKG" "${args[@]}"
done

echo
echo "产物目录："
for triplet in "${triplets[@]}"; do
    case "$triplet" in
        arm64-android)               platform=android ;;
        arm64-ios|arm64-ios-simulator) platform=ios ;;
        arm64-ohos)                  platform=ohos ;;
    esac
    printf '  %-24s %s\n' "$triplet" "$INSTALL_ROOT/$platform/$triplet"
done