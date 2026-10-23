#!/usr/bin/env bash
# 可选：把 vcpkg 工具克隆到项目内 vcpkg/tool，并钉在与现有产物一致的提交。
# 不运行也能构建：scripts/env.sh 会自动使用 ~/ENV/vcpkg。
set -euo pipefail

VCPKG_COMMIT="5300a2a461a53b76405db4fcbe6aeb0eea43935d"
VCPKG_WORKSPACE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOL_DIR="$VCPKG_WORKSPACE/tool"

if [[ -x "$TOOL_DIR/vcpkg" ]]; then
    echo "已存在：$TOOL_DIR/vcpkg"
    exit 0
fi

git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "$TOOL_DIR"
git -C "$TOOL_DIR" checkout "$VCPKG_COMMIT"
"$TOOL_DIR/bootstrap-vcpkg.sh" -disableMetrics

echo "完成：$TOOL_DIR/vcpkg"