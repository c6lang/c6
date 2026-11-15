#!/bin/sh
# tcc 上游 tests2 记分板：用 c6c 编 tcc/tests/tests2/*.c，比 stdout 与 *.expect。
#
# 这是目标（3）的度量尺：现在多数用例会因为「还不能链接 libc」失败，
# 但每次提交都能看到分子/分母在动，而不是等到最后才知道差多少。
#
# 用法：tests/tcc-tests2.sh [-v] [用例名前缀]
set -u
CC=${C6C:-build/c6c}
HERE=$(cd "$(dirname "$0")" && pwd)
TCCDIR=${TCCDIR:-$HERE/../../tcc/tests/tests2}
VERBOSE=0
FILTER=""
NA=$HERE/tcc-na.txt
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
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# 每个用例的结论写进 $TMP/v.<序号>（一行），并行时不打乱、不抢计数器；
# 汇总按用例名顺序读回来，报告与串行逐字节一致。
one_case() {
    src=$1
    vf=$2
    base=$(basename "$src" .c)
    : >"$vf"

    if ! "$CC" "$src" -o "$TMP/$base" >"$TMP/$base.log" 2>"$TMP/$base.err"; then
        if grep -q '未定义的符号' "$TMP/$base.err"; then
            echo "LINK $(grep -o "未定义的符号 '[^']*'" "$TMP/$base.err" | head -1)" >"$vf"
            grep -o "未定义的符号 '[^']*'" "$TMP/$base.err" | sed "s/.*'\(.*\)'/\1/" \
                >"$TMP/miss.$base"
        elif grep -q '没有 main' "$TMP/$base.err"; then
            # 库用例：源文件本来就没有 main（104+_inline 之类），上游也是只编不跑。
            # 判据是"编译期干净"，所以走 -fsyntax-only 再验一遍。
            if "$CC" -fsyntax-only "$src" >"$TMP/$base.syn" 2>&1; then
                echo "NOLINK" >"$vf"
            else
                echo "CERR $(head -1 "$TMP/$base.syn")" >"$vf"
            fi
        else
            echo "CERR $(head -1 "$TMP/$base.err")" >"$vf"
        fi
        return
    fi
    exp="$TCCDIR/$base.expect"
    [ -e "$exp" ] || exp=/dev/null
    # 少数用例要带命令行参数（对齐上游 tests2/Makefile 里的 ARGS）
    args=""
    case "$base" in
    31_args) args="arg1 arg2 arg3 arg4 arg5" ;;
    esac
    # 编译期**告警**要进输出：上游的 .expect 里带着它们（`03_struct.expect`
    # 第一行就是那条 `__cleanup__ ignored on type`）。路径前缀按 expect 的
    # 口径去掉（我们编的时候用的是完整路径）。
    : > "$TMP/$base.out"
    if [ -s "$TMP/$base.err" ]; then
        sed "s|$TCCDIR/||g" "$TMP/$base.err" | grep -v '^$' >> "$TMP/$base.out" 2>/dev/null
    fi
    ( cd "$TMP" && timeout 10 "./$base" $args >>"$TMP/$base.out" 2>"$TMP/$base.runerr" )
    if diff -q "$exp" "$TMP/$base.out" >/dev/null 2>&1; then
        echo "PASS" >"$vf"
    else
        # 先排除"expect 文件本身和源码不符"的情况：用 gcc 编同一份跑一遍，
        # gcc 的输出对不上 expect，就不是 c6c 的错（仓库里的 expect 是旧的）。
        stale=0
        if command -v gcc >/dev/null 2>&1; then
            if gcc -w -o "$TMP/g_$base" "$src" 2>/dev/null; then
                ( cd "$TMP" && "$TMP/g_$base" >"$TMP/$base.gccout" 2>/dev/null )
                if ! diff -q "$exp" "$TMP/$base.gccout" >/dev/null 2>&1; then
                    stale=1
                fi
            fi
        fi
        if [ "$stale" = 1 ]; then
            echo "STALE" >"$vf"
        else
            echo "FAIL $(diff "$exp" "$TMP/$base.out" 2>&1 | head -2 | tr '\n' ' ')" >"$vf"
        fi
    fi
}

i=0
for src in "$TCCDIR"/*.c; do
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
fail=0
na=0
cerr=0
linkerr=0
cstale=0
nolink=0
i=0
for src in "$TCCDIR"/*.c; do
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
    case "$vl" in
    PASS) pass=$((pass + 1)) ;;
    NOLINK) nolink=$((nolink + 1))
        [ "$VERBOSE" = 1 ] && printf '仅编译   %-20s 无 main（上游也只编不跑）\n' "$base" ;;
    *)
        # 非通过项里，凡是在 tcc-na.txt 里登记过"架构上不可达"的，单独算一类。
        [ -f "$TMP/miss.$base" ] && cat "$TMP/miss.$base" >>"$TMP/missing.txt"
        if [ -f "$NA" ] && grep -q "^$base	" "$NA" 2>/dev/null; then
            na=$((na + 1))
            [ "$VERBOSE" = 1 ] && printf 'n/a      %-20s %s\n' "$base" \
                "$(grep "^$base	" "$NA" | cut -f2)"
        elif [ "$vl" = "STALE" ]; then
            cstale=$((cstale + 1))
        elif [ "${vl#LINK}" != "$vl" ]; then
            fail=$((fail + 1))
            [ "$VERBOSE" = 1 ] && printf '意外失败 %-20s %s（不在 tcc-na.txt 里）\n' "$base" "$vl"
        elif [ "${vl#CERR}" != "$vl" ]; then
            fail=$((fail + 1))
            [ "$VERBOSE" = 1 ] && printf '意外失败 %-20s %s（不在 tcc-na.txt 里）\n' "$base" "${vl#CERR }"
        else
            fail=$((fail + 1))
            [ "$VERBOSE" = 1 ] && printf '意外失败 %-20s %s（不在 tcc-na.txt 里）\n' "$base" "${vl#FAIL }"
        fi
        ;;
    esac
done
total=$((pass + fail + cerr + linkerr + cstale + nolink + na))
echo "tcc tests2：通过 $pass / 架构性 n/a $na / 仅编译 $nolink / 上游 expect 旧 $cstale / 意外失败 $fail （共 $total）"
if [ -s "$TMP/missing.txt" ]; then
    echo "最缺的外部符号（前 12 个）：$(sort "$TMP/missing.txt" | uniq -c | sort -rn | head -12 | awk '{printf "%s(%s) ", $2, $1}')"
fi
[ $((pass + nolink + cstale + na)) -eq "$total" ]
