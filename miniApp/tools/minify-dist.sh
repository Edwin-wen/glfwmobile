#!/usr/bin/env bash
# 压缩 miniApp/dist 的 JS 产物。
# 微信主包上限 2M、建议 1.5M，而 Kuikly 的 webpack 产物默认未压缩（lib+business ≈ 3.9M）。
# 产物结构不变，只做 compress + mangle，不影响 UMD 导出（属性名不混淆）。
set -euo pipefail

DIST_DIR="${1:-$(cd "$(dirname "$0")/.." && pwd)/dist}"
TERSER_OPTS=(--compress --mangle --comments false)

total_before=0
total_after=0

for js in "$DIST_DIR"/lib/*.js "$DIST_DIR"/business/*.js; do
  [ -f "$js" ] || continue

  before=$(wc -c <"$js")
  # 先写临时文件再替换，避免 terser 读写同一文件时截断输入
  npx --yes terser@5 "$js" "${TERSER_OPTS[@]}" -o "$js.min" >/dev/null
  mv "$js.min" "$js"
  after=$(wc -c <"$js")

  # 压缩产物不再提供调试信息，sourcemap 引用一并去掉
  rm -f "$js.map"

  total_before=$((total_before + before))
  total_after=$((total_after + after))
  printf '  %-40s %7s -> %7s\n' "${js#"$DIST_DIR"/}" \
    "$((before / 1024))K" "$((after / 1024))K"
done

printf 'JS total: %sK -> %sK\n' "$((total_before / 1024))" "$((total_after / 1024))"