#!/bin/sh
# printf 口径回归：c6c 的运行时（c6c/lib/rt.c6 的 c6c_vfmt）和 glibc 对齐吗？
#
# 做法很直白：同一份 `printf-matrix.c`（只用 C 公共子集）用宿主 cc 和 c6c 各编
# 一遍、各跑一遍，**逐行 diff**。不一样的每一行都是格式化的口径差异——这类差异
# 在 std 的用例里表现为"自检里只差一两条"，很难顺藤摸瓜，所以单独钉在这里。
#
# 断言：
#   1. 参考侧（cc + glibc）跑通；
#   2. c6c 侧跑通；
#   3. 两侧的**行数**一致（少一行就是少印/多印了换行）；
#   4. 逐行逐字节一致；
#   5. 退出码一致。
set -u

C6C=${C6C:-./build/c6c}
CC=${CC:-cc}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/printf-matrix.c
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
TMPDIR="$WORK/tmp"
export TMPDIR
mkdir -p "$TMPDIR"

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }

have() { command -v "$1" >/dev/null 2>&1; }

case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac

if [ ! -f "$SRC" ]; then
    bad "找到矩阵源码" "$SRC 不存在"
    echo "c6c printf 口径回归：通过 $pass / 失败 $fail"
    exit 1
fi

# 参考侧：宿主 cc + 系统 libc（glibc/musl 都行，反正以它为准）
if have "$CC"; then
    if "$CC" -w -O1 -o "$WORK/ref" "$SRC" >"$WORK/ref.log" 2>&1; then
        ok "参考侧编译（$CC）"
    else
        bad "参考侧编译（$CC）" "$(tail -2 "$WORK/ref.log")"
    fi
    if "$WORK/ref" >"$WORK/ref.txt" 2>"$WORK/ref.err"; then
        ok "参考侧运行"
    else
        bad "参考侧运行" "$(tail -2 "$WORK/ref.err")"
    fi
else
    bad "参考侧编译" "没有 $CC，无法给出基准"
fi

# 被测侧：c6c
if "$C6C" -o "$WORK/c6out" "$SRC" >"$WORK/c6.log" 2>&1; then
    ok "c6c 编译矩阵"
else
    bad "c6c 编译矩阵" "$(grep -v 'warning: type defaults' "$WORK/c6.log" | tail -2)"
fi
if "$WORK/c6out" >"$WORK/c6.txt" 2>"$WORK/c6.err"; then
    ok "c6c 运行矩阵"
else
    bad "c6c 运行矩阵" "$(tail -2 "$WORK/c6.err")"
fi

if [ -f "$WORK/ref.txt" ] && [ -f "$WORK/c6.txt" ]; then
    nl_ref=$(wc -l <"$WORK/ref.txt")
    nl_c6=$(wc -l <"$WORK/c6.txt")
    if [ "$nl_ref" = "$nl_c6" ]; then
        ok "行数一致（$nl_ref 行）"
    else
        bad "行数一致" "glibc $nl_ref 行 / c6c $nl_c6 行"
    fi
    if diff -u "$WORK/ref.txt" "$WORK/c6.txt" >"$WORK/diff.txt" 2>&1; then
        ok "逐行一致（$nl_ref 条格式）"
    else
        bad "逐行一致" "$(grep -c '^-[^-]' "$WORK/diff.txt") 行不同"
        echo "--- 前 12 行差异 ---"
        head -12 "$WORK/diff.txt"
        echo "---------------------"
    fi
fi

echo "c6c printf 口径回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
