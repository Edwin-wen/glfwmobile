#!/usr/bin/env bash
# 构建 CanvasKit（Skia m134 的 wasm + JS bindings），供 H5 / 微信小程序接入。
#
# 说明：vcpkg 的 skia 端口只能产出 wasm 静态库，不能产出 CanvasKit 的 JS 绑定；
# 所以这条链路直接用 Skia 官方 GN + pinned emsdk，产物仍放在 vcpkg/installed 下。
set -euo pipefail

VCPKG_WORKSPACE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WASM_ROOT="$VCPKG_WORKSPACE/wasm"
SKIA="$WASM_ROOT/skia"
OUT="$WASM_ROOT/out/canvaskit_wasm"
DEST="$VCPKG_WORKSPACE/installed/wasm/canvaskit"

SKIA_REF="aefbd9403c1b3032ad4cd0281ef312ed262c7125"
SKIA_TARBALL="${SKIA_TARBALL:-$HOME/ENV/vcpkg/downloads/google-skia-$SKIA_REF.tar.gz}"

find_tool() {
    local name="$1"
    shift
    local found
    found="$(find "$@" -name "$name" -type f 2>/dev/null | head -1 || true)"
    [[ -n "$found" && -x "$found" ]] && echo "$found"
}

GN="${GN:-$(find_tool gn "$VCPKG_WORKSPACE/installed" "$HOME/ENV/vcpkg/installed")}"
[[ -n "$GN" ]] || GN="$(command -v gn || true)"
NINJA="${NINJA:-$(find_tool ninja "$HOME/ENV/vcpkg/downloads/tools/ninja")}"
[[ -n "$NINJA" ]] || NINJA="$(command -v ninja || true)"
[[ -n "$GN" ]] || { echo "找不到 gn：请安装 vcpkg 的 vcpkg-tool-gn，或设置 GN" >&2; exit 1; }
[[ -n "$NINJA" ]] || { echo "找不到 ninja：请设置 NINJA" >&2; exit 1; }

# 1) Skia 源码：优先复用 vcpkg 已下载的源码包，避免再次 clone。
if [[ ! -f "$SKIA/DEPS" ]]; then
    mkdir -p "$WASM_ROOT"
    if [[ -f "$SKIA_TARBALL" ]]; then
        echo "解压 $SKIA_TARBALL"
        tar -xzf "$SKIA_TARBALL" -C "$WASM_ROOT"
        mv "$WASM_ROOT/skia-$SKIA_REF" "$SKIA"
    else
        echo "克隆 Skia@$SKIA_REF"
        git clone --filter=blob:none https://github.com/google/skia.git "$SKIA"
        git -C "$SKIA" checkout "$SKIA_REF"
    fi
fi

# 2) CanvasKit 只需要的 DEPS（不拉 dawn/angle/swiftshader 等大仓库）。
cp "$WASM_ROOT/DEPS.canvaskit" "$SKIA/DEPS.canvaskit"
if [[ ! -x "$SKIA/third_party/externals/emsdk/upstream/emscripten/emcc" ]] \
    || [[ ! -f "$SKIA/third_party/externals/harfbuzz/src/hb.hh" ]]; then
    echo "同步 CanvasKit DEPS（含 emsdk 3.1.44）"
    (cd "$SKIA" && GIT_SYNC_DEPS_PATH="$SKIA/DEPS.canvaskit" python3 tools/git-sync-deps)
fi

# 3) Emscripten 环境
# shellcheck disable=SC1091
source "$SKIA/third_party/externals/emsdk/emsdk_env.sh" >/dev/null 2>&1
echo "emcc: $(emcc --version | head -1)"

# 4) GN + Ninja
mkdir -p "$OUT"
cp "$WASM_ROOT/canvaskit.args.gn" "$OUT/args.gn"
(cd "$SKIA" && "$GN" gen "$OUT")
"$NINJA" -C "$OUT" canvaskit.js

# 5) 产物
mkdir -p "$DEST"
cp "$OUT/canvaskit.js" "$OUT/canvaskit.wasm" "$DEST/"
if [[ -f "$SKIA/modules/canvaskit/npm_build/types/index.d.ts" ]]; then
    cp "$SKIA/modules/canvaskit/npm_build/types/index.d.ts" "$DEST/canvaskit.d.ts"
fi

{
    echo "skia_ref=$SKIA_REF"
    echo "emcc=$(emcc --version | head -1)"
    echo "built_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "wasm_sha256=$(shasum -a 256 "$DEST/canvaskit.wasm" | awk '{print $1}')"
    echo "js_sha256=$(shasum -a 256 "$DEST/canvaskit.js" | awk '{print $1}')"
} > "$DEST/build-info.txt"

echo
ls -lh "$DEST"
echo "产物目录：$DEST"