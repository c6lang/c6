#!/bin/sh
# std/ 的每个模块都是**独立编译**（`-fsyntax-only`），互不共享状态，所以可以
# 并行：`-j N`（默认 1）。结论按路径序汇总，报告与串行完全一致。
set -u
C6C=${C6C:-./build/c6c}
ROOT=${ROOT:-..}
JOBS=${JOBS:-1}
# 编译一个模块的上限（秒）：编译器卡死时由它出手。
C6C_TMO=${C6C_TMO:-120}
while [ $# -gt 0 ]; do
    case "$1" in
    -j) shift; [ $# -gt 0 ] && JOBS=$1 ;;
    -j*) JOBS=${1#-j} ;;
    esac
    shift
done
case "$JOBS" in ''|*[!0-9]*) JOBS=1 ;; esac
[ "$JOBS" -ge 1 ] || JOBS=1
TMP=${TMPDIR:-/tmp}/c6c-std.$$
mkdir -p "$TMP" || exit 1
trap 'rm -rf "$TMP"' EXIT

i=0
nrun=0
for f in $(find "$ROOT/std" -name '*.c6' | sort); do
    i=$((i + 1))
    (
        rc=0
        timeout -k 5 "$C6C_TMO" "$C6C" -fsyntax-only -I"$ROOT" -I"$ROOT/std" "$f" \
            >"$TMP/l.$i" 2>&1 || rc=$?
        if [ "$rc" -eq 0 ]; then
            : >"$TMP/r.$i"
        elif [ -s "$TMP/l.$i" ]; then
            head -1 "$TMP/l.$i" >"$TMP/r.$i"
        else
            # 没有诊断输出（多半是超时被打死）：也必须算失败。
            # 以前 `head -1` 空文件 → r.$i 是空的 → 汇总按"通过"算。
            echo "编不过（无诊断输出，rc=$rc，可能是超时 >${C6C_TMO}s）" >"$TMP/r.$i"
        fi
    ) &
    # 限流只能自己数：`$(jobs -p | wc -l)` 在 dash 下恒为 0（命令替换是子
    # shell，看不见父 shell 的作业表），那一版等于没有限流。
    nrun=$((nrun + 1))
    if [ "$nrun" -ge "$JOBS" ]; then
        wait
        nrun=0
    fi
done
wait

ok=0
bad=0
first=""
i=0
for f in $(find "$ROOT/std" -name '*.c6' | sort); do
    i=$((i + 1))
    if [ -s "$TMP/r.$i" ]; then
        bad=$((bad + 1))
        [ -z "$first" ] && first=$f
    else
        ok=$((ok + 1))
    fi
done
echo "c6c: std 模块 通过 $ok / 失败 $bad"
[ "$bad" -ne 0 ] && echo "c6c: 第一个失败样例：$first"
[ "$bad" -eq 0 ]
