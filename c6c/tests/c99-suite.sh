#!/bin/sh
# 语言层 C99 外部闸门：c-testsuite（third_party/c-testsuite/，scc 来源 150 条）。
#
# 上游接口（tests/single-exec）：编一个可执行 → 跑 → 程序 stdout+stderr 必须
# 逐字节等于 *.c.expected，且退出码 0。这里照抄这个口径，只加分类：
#
#   PASS   与 .expected 完全一致
#   LANG   前端编不过（语法/语义/诊断问题）—— 这是"语言 100% 兼容 C99"要清掉的
#   OUT    编过了但跑出来不一样 —— 可能是代码生成错，也可能是库行为差异（见下）
#   LIB    链接期缺符号（运行期没实现）—— 按"库不管"的口径，这类不算语言问题，
#          但单独计数，免得和代码生成错混在一起
#   STALE  上游 .expected 本身过期：拿 gcc -std=c99 编同一份、跑出来也对不上。
#          这类是套件自己的账，不算 c6c 的
#   SKIP   gcc -std=c99 都编不过（用例不是合法 C99，常见于故意写错的负例）
#
# 判定顺序保证了"不是 c6c 的错"先被摘出去：先用 gcc 当 oracle，再把 c6c 的
# 失败按 LANG/LIB 分开，最后才比输出。
#
# 用法：
#   tests/c99-suite.sh [-v] [-j N] [用例名前缀]
#   C6C=build/c6c ROOT=$PWD ./tests/c99-suite.sh
#
# 退出码：LANG 或 OUT 非 0 时退出 1（STALE/SKIP/LIB 不挡）。LIB 单列是因为
# 目标口径是"语言层 100%，库不管"：库缺符号不应该掩盖语言层的红。
set -u

C6C=${C6C:-build/c6c}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${ROOT:-$HERE/../..}
SUITE=${SUITE:-$ROOT/third_party/c-testsuite/single-exec}
ORACLE_CC=${ORACLE_CC:-gcc}
VERBOSE=0
FILTER=""
JOBS=${JOBS:-1}

while [ $# -gt 0 ]; do
    case "$1" in
    -v) VERBOSE=1 ;;
    -j) shift; [ $# -gt 0 ] && JOBS=$1 ;;
    -j*) JOBS=${1#-j} ;;
    *) FILTER=$1 ;;
    esac
    shift
done
case "$JOBS" in ''|*[!0-9]*) JOBS=1 ;; esac
[ "$JOBS" -ge 1 ] || JOBS=1

if [ ! -d "$SUITE" ]; then
    echo "找不到用例目录：$SUITE" >&2
    echo "（先确认 third_party/c-testsuite 在仓库里；ROOT=$ROOT）" >&2
    exit 2
fi
if [ ! -x "$C6C" ]; then
    echo "找不到可执行的 C6C：$C6C（先 make build）" >&2
    exit 2
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# 每条用例的结论写进 $TMP/v.<序号>（一行），并行时不打乱计数器。
one_case() {
    src=$1
    vf=$2
    base=$(basename "$src" .c)
    exp="$src.expected"
    [ -e "$exp" ] || exp=/dev/null
    : >"$vf"

    # 1) gcc 当 oracle：编不过 ⇒ 用例不是合法 C99
    if ! "$ORACLE_CC" -std=c99 -w -o "$TMP/g_$base" "$src" >"$TMP/g_$base.log" 2>&1; then
        echo "SKIP oracle 编不过" >"$vf"
        return
    fi
    # 2) oracle 跑出来的输出对不上 .expected（或退出码非 0）⇒ 上游 expect 过期
    #    协议要求"成功时退出码 0"，所以退出码也是判据的一部分。
    : >"$TMP/g_$base.out"
    ( cd "$TMP" && timeout 10 "./g_$base" >>"$TMP/g_$base.out" 2>&1 )
    grc=$?
    if [ "$grc" -ne 0 ]; then
        echo "STALE oracle 退出码 $grc（用例本身不满足“成功即 0”）" >"$vf"
        return
    fi
    if ! diff -q "$exp" "$TMP/g_$base.out" >/dev/null 2>&1; then
        echo "STALE $(diff "$exp" "$TMP/g_$base.out" 2>&1 | head -2 | tr '\n' ' ')" >"$vf"
        return
    fi

    # 3) c6c 编：缺符号按 LIB 记，其余是语言层失败
    if ! "$C6C" "$src" -o "$TMP/c_$base" >"$TMP/c_$base.log" 2>"$TMP/c_$base.err"; then
        if grep -q '未定义的符号\|undefined symbol' "$TMP/c_$base.err"; then
            echo "LIB $(grep -o "未定义的符号 '[^']*'" "$TMP/c_$base.err" | head -1)" >"$vf"
        else
            echo "LANG $(head -1 "$TMP/c_$base.err")" >"$vf"
        fi
        return
    fi

    # 4) 跑：stdout+stderr 合并后与 .expected 比，且退出码必须是 0
    : >"$TMP/c_$base.out"
    ( cd "$TMP" && timeout 10 "./c_$base" >>"$TMP/c_$base.out" 2>&1 )
    crc=$?
    if [ "$crc" -ne 0 ]; then
        # 死法要说清楚：139 = SIGSEGV，124 = 超时，其余照抄
        echo "OUT 退出码 $crc（输出$(diff -q "$exp" "$TMP/c_$base.out" >/dev/null 2>&1 && echo "对得上" || echo "也对不上")）" >"$vf"
        return
    fi
    if diff -q "$exp" "$TMP/c_$base.out" >/dev/null 2>&1; then
        echo "PASS" >"$vf"
    else
        echo "OUT $(diff "$exp" "$TMP/c_$base.out" 2>&1 | head -2 | tr '\n' ' ')" >"$vf"
    fi
}

i=0
for src in "$SUITE"/*.c; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .c)
    case "$base" in
    *"$FILTER"*) ;;
    *) continue ;;
    esac
    i=$((i + 1))
    if [ "$JOBS" -le 1 ]; then
        one_case "$src" "$TMP/v.$i"
    else
        one_case "$src" "$TMP/v.$i" &
        if [ "$(jobs -p | wc -l)" -ge "$JOBS" ]; then
            wait
        fi
    fi
done
wait

pass=0
lang=0
out=0
lib=0
stale=0
skip=0
i=0
for src in "$SUITE"/*.c; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .c)
    case "$base" in
    *"$FILTER"*) ;;
    *) continue ;;
    esac
    i=$((i + 1))
    v="$TMP/v.$i"
    [ -f "$v" ] || continue
    vl=$(cat "$v")
    tags=""
    [ -f "$src.tags" ] && tags=$(tr '\n' ',' <"$src.tags" | sed 's/,$//')
    case "$vl" in
    PASS*) pass=$((pass + 1)) ;;
    LANG*) lang=$((lang + 1))
        printf 'LANG   %-8s %s\n' "$base" "${vl#LANG }"
        [ -n "$tags" ] && printf '       tags: %s\n' "$tags" ;;
    OUT*) out=$((out + 1))
        printf 'OUT    %-8s %s\n' "$base" "${vl#OUT }"
        [ -n "$tags" ] && printf '       tags: %s\n' "$tags" ;;
    LIB*) lib=$((lib + 1))
        [ "$VERBOSE" = 1 ] && printf 'LIB    %-8s %s\n' "$base" "${vl#LIB }" ;;
    STALE*) stale=$((stale + 1))
        [ "$VERBOSE" = 1 ] && printf 'STALE  %-8s %s\n' "$base" "${vl#STALE }" ;;
    SKIP*) skip=$((skip + 1))
        [ "$VERBOSE" = 1 ] && printf 'SKIP   %-8s %s\n' "$base" "${vl#SKIP }" ;;
    *) ;;
    esac
done

total=$((pass + lang + out + lib + stale + skip))
echo
echo "c-testsuite（语言层 C99）：通过 $pass / 语言层失败 $lang / 输出不符 $out" \
    "/ 库缺符号 $lib / 上游 expect 旧 $stale / 用例不成立 $skip（共 $total）"
if [ "$lang" -ne 0 ] || [ "$out" -ne 0 ]; then
    echo "闸门：红（LANG+OUT = $((lang + out))，语言层 C99 还没到 100%）"
    exit 1
fi
echo "闸门：绿（语言层 100%：没有 LANG/OUT）"
exit 0
