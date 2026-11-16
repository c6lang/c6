#!/bin/sh
# IR dump 稳定性：两次运行必须逐字节相同（IR 是契约，编码变了就是破坏兼容）。
# 从 c6c/Makefile 的 irtest 目标搬过来的，口径没变。
set -u
C6C=${C6C:?用法: C6C=build/c6c OUT=build tests/ir-demo-stable.sh}
OUT=${OUT:?需要 OUT=输出目录}
"$C6C" --ir-demo > "$OUT/ir-demo-1.txt"
"$C6C" --ir-demo > "$OUT/ir-demo-2.txt"
cmp "$OUT/ir-demo-1.txt" "$OUT/ir-demo-2.txt" || {
    echo "c6c: 两次 --ir-demo 的输出不一致"
    exit 1
}
echo "c6c: IR dump 稳定（两次运行逐字节相同）"
