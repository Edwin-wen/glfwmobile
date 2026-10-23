#!/usr/bin/env bash
# 三端构建共用的路径与工具链解析。所有变量都可以用环境变量覆盖。
set -euo pipefail

VCPKG_WORKSPACE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# vcpkg 工具根目录：优先项目内 tool/，其次本机已有的 ~/ENV/vcpkg。
if [[ -z "${VCPKG_ROOT:-}" ]]; then
    if [[ -x "$VCPKG_WORKSPACE/tool/vcpkg" ]]; then
        VCPKG_ROOT="$VCPKG_WORKSPACE/tool"
    elif [[ -x "$HOME/ENV/vcpkg/vcpkg" ]]; then
        VCPKG_ROOT="$HOME/ENV/vcpkg"
    else
        echo "未找到 vcpkg：请先运行 vcpkg/scripts/bootstrap.sh，或设置 VCPKG_ROOT" >&2
        exit 1
    fi
fi
export VCPKG_ROOT
VCPKG="$VCPKG_ROOT/vcpkg"

ANDROID_SDK="${ANDROID_SDK:-$HOME/ENV/AndroidSdk}"
if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
    ANDROID_NDK_HOME="$(ls -d "$ANDROID_SDK"/ndk/27.* 2>/dev/null | tail -1 || true)"
fi
export ANDROID_NDK_HOME

OHOS_SDK_NATIVE="${OHOS_SDK_NATIVE:-/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native}"
export OHOS_SDK_NATIVE

# 与已有 Android 产物保持同一 CMake（vcpkg ABI 里包含 CMake 版本）。
ANDROID_CMAKE_BIN="$ANDROID_SDK/cmake/3.30.3/bin"
# OHOS 必须用 SDK 自带 CMake：系统 CMake 没有 Platform/OHOS.cmake。
OHOS_CMAKE_BIN="$OHOS_SDK_NATIVE/build-tools/cmake/bin"

INSTALL_ROOT="$VCPKG_WORKSPACE/installed"

require_file() {
    [[ -e "$1" ]] || { echo "缺少依赖：$1" >&2; exit 1; }
}