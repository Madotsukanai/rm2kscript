#!/usr/bin/env bash
# tests/roundtrip.sh — decode→encode→decode のテキスト一致テスト
#
# 使い方:
#   bash tests/roundtrip.sh <rm2kscript バイナリ> <テスト用 .lmu ディレクトリ>
#
# 終了コード: 0 = 全テスト通過, 非0 = 失敗あり

set -euo pipefail

TOOL="${1:?usage: $0 <tool> <lmu_dir>}"
LMU_DIR="${2:?usage: $0 <tool> <lmu_dir>}"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

pass=0
fail=0
errors=()

for lmu in "$LMU_DIR"/*.lmu; do
    base=$(basename "$lmu")

    # decode → encode → decode
    "$TOOL" decode "$lmu"                    -o "$TMPDIR/p1.rs2k" 2>/dev/null
    cp "$lmu" "$TMPDIR/rt.lmu"
    "$TOOL" encode "$TMPDIR/p1.rs2k" "$TMPDIR/rt.lmu" \
                                             -o "$TMPDIR/rt2.lmu" 2>/dev/null
    "$TOOL" decode "$TMPDIR/rt2.lmu"         -o "$TMPDIR/p2.rs2k" 2>/dev/null

    if diff -q "$TMPDIR/p1.rs2k" "$TMPDIR/p2.rs2k" >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        errors+=("$base")
        echo "FAIL: $base"
        diff "$TMPDIR/p1.rs2k" "$TMPDIR/p2.rs2k" | head -6
    fi
done

echo ""
echo "Result: $pass passed, $fail failed"

if [[ ${#errors[@]} -gt 0 ]]; then
    echo "Failed files:"
    printf '  %s\n' "${errors[@]}"
    exit 1
fi
