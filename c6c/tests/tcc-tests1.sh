#!/bin/sh
# tcc 上游 tests1 口径的记分板（tests/ 下那些"老式"单文件用例：
# tcctest.c / abitest.c / boundtest.c / bug.c / vla_test.c / testfp.c …）。
#
# 和 tests2 的区别：这批用例很多要 tcc 自己的 libtcc1.a、内部头文件（tcc.h）
# 或 -run 语义，x86-64 上本来就不是全绿；这里只如实分档，给"（3）c6c 直接跑
# 上游用例"一把尺子。
#
# 用法：tests/tcc-tests1.sh [-v] [用例名前缀]
set -u
CC=${C6C:-build/c6c}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${ROOT:-$HERE/../../..}
DIR=${T1DIR:-$ROOT/tcc/tests}
VERBOSE=0
FILTER=""
for a in "$@"; do
    case "$a" in
    -v) VERBOSE=1 ;;
    *) FILTER=$a ;;
    esac
done
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0
cerr=0
nolink=0
skip=0
for src in "$DIR"/*.c; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .c)
    case "$base" in
    *"$FILTER"*) ;;
    *) continue ;;
    esac
    # 要 tcc 自身构建产物/多 TU 的用例先记"跳过"（不是 c6c 的问题）
    case "$base" in
    libtcc_test|libtcc_test_mt|asm-c-connect-1|asm-c-connect-2|msvcrt_start|tcctest)
        skip=$((skip + 1))
        [ $VERBOSE = 1 ] && printf '跳过     %-18s 需要 tcc 自身构建产物/多 TU\n' "$base"
        continue ;;
    esac
    if ! "$CC" -I"$DIR" -I"$ROOT/tcc" "$src" -o "$TMP/$base" >"$TMP/$base.log" 2>&1; then
        if grep -q '未定义的符号' "$TMP/$base.log"; then
            nolink=$((nolink + 1))
            [ $VERBOSE = 1 ] && printf '缺符号   %-18s %s\n' "$base" \
                "$(grep -o "未定义的符号 '[^']*'" "$TMP/$base.log" | head -1)"
        else
            cerr=$((cerr + 1))
            [ $VERBOSE = 1 ] && printf '编不过   %-18s %s\n' "$base" "$(head -1 "$TMP/$base.log")"
        fi
        continue
    fi
    ( cd "$TMP" && timeout 30 "./$base" >"$TMP/$base.out" 2>&1 )
    rc=$?
    if [ "$rc" -eq 0 ]; then
        pass=$((pass + 1))
        [ $VERBOSE = 1 ] && printf '通过     %s\n' "$base"
    else
        fail=$((fail + 1))
        [ $VERBOSE = 1 ] && printf '运行失败 %-18s 退出码 %d\n' "$base" "$rc"
    fi
done
total=$((pass + fail + cerr + nolink + skip))
echo "tcc tests1：通过 $pass / 运行失败 $fail / 编不过 $cerr / 缺外部符号 $nolink / 跳过 $skip （共 $total）"
[ "$cerr" -eq 0 ] && [ "$fail" -eq 0 ]
