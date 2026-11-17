#!/bin/sh
# 用 **c6c** 跑 `tests/regression.sh` 的用例（交付物要能不带 tcc，就得让这批
# 回归也能只靠 c6c 跑）。
#
# 三个用法：
#   ./tests/regression-c6c.sh                  # 跑 regression-c6c.list 里那批（已全绿）
#   ./tests/regression-c6c.sh --all            # 跑全部 std-* 用例，量基线
#   ./tests/regression-c6c.sh std-demo std-web # 只跑点名的几个
#
# 环境变量：
#   C6C    被测编译器，默认 ./build/c6c（CMake 目标会注入实际路径）
#   C6_TMP 临时目录根，默认 $TMPDIR
#
# 口径：每个用例一个独立输出目录（C6_CASE_OUT），跑完打印 `ok` / `FAIL` 行，
# 最后一行是「通过 N / 失败 M」，失败数当退出码。它只调度，不解释失败原因——
# 原因在 tests/regression.sh 的用法说明里：`C6_CC=… tests/regression.sh <用例>`。
set -u

C6C=${C6C:-./build/c6c}
case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac
HERE=$(cd "$(dirname "$0")/../.." && pwd)
LIST="$HERE/c6c/tests/regression-c6c.list"
OUT=${C6_CASE_ROOT:-$HERE/build/regression-c6c}
TMPBASE=${C6_TMP:-${TMPDIR:-/tmp}}
mkdir -p "$OUT" "$TMPBASE"

pass=0
fail=0
failed=""

run_case() {
    c=$1
    o=$OUT/$c
    rm -rf "$o"
    mkdir -p "$o"
    if C6_CC=$C6C C6_TCC_DIR=tcc C6_CASE_OUT="$o" C6_TMP="$TMPBASE/rc6c-$c" \
            "$HERE/tests/regression.sh" "$c" >"$o.log" 2>&1; then
        pass=$((pass + 1))
        echo "ok   $c"
    else
        fail=$((fail + 1))
        failed="$failed $c"
        echo "FAIL $c（日志 $o.log 末行：$(grep -v 'warning: type defaults' "$o.log" | tail -1)）"
    fi
}

case "${1:-}" in
--all)
    for c in $(sed -n 's/^\(std-[a-z0-9-]*\)$/\1/p' "$HERE/tests/regression.sh"); do
        run_case "$c"
    done
    ;;
"")
    n=0
    while read -r line; do
        case "$line" in
        ""|\#*) continue ;;
        esac
        n=$((n + 1))
        run_case "$line"
    done <"$LIST"
    if [ "$n" = 0 ]; then
        echo "FAIL $LIST 里一个用例都没有"
        exit 1
    fi
    ;;
*)
    for c in "$@"; do
        run_case "$c"
    done
    ;;
esac

echo "c6c 回归：通过 $pass / 失败 $fail${failed:+（失败：$failed）}"
[ "$fail" -eq 0 ]
