#!/bin/sh
# check-all 的并发调度器：把完整验收拆成五条「车道」，车道之间并发跑，
# 车道内部仍然串行（用 make -j1）。
#
# 为什么要分车道、而不是直接 make -j：
#   * tests 家族（selftest / c99test / c99tcc-test）各自用 $$ 命名的 $TMP，
#     互不干扰，可以同时跑；
#   * std 家族（stdtest + 八十多个 std-* ）的用例会往 build/ 下的**固定**
#     临时路径写文件（build/fs-test/、build/std-static/、build/mmap-test.bin
#     ……），只能一条一条跑；
#   * 裸机五连（boot / tests / go / handle / smp）过去共用 build/bare.img，
#     并发会互相盖掉，现在每条一个目录（BARE_BUILD）才敢并发。
# 于是并发的单位是「车道」，不是「目标」。
#
#   make check-all                 五条车道并发（默认）
#   CHECK_SERIAL=1 make check-all  照样按车道跑，但一条跑完再跑下一条
#
# 目标列表由 Makefile 通过 CHECK_LANE_* 环境变量传进来（脚本里不再抄一份，
# 免得 test-all / check-all / help 之后又多一处要同步）。
#
# 每条车道的输出都带 [车道名] 前缀，行级交错但不会混行；失败的用例照样
# 会打印 FAIL 行，最后再给一张汇总表。
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

lanes="tests tests2 selfhost std bare"
serial=${CHECK_SERIAL:-0}

case $serial in
0) ;;
1) ;;
*) echo "check-all: CHECK_SERIAL 只能是 0 或 1（现在是 '$serial'）" >&2; exit 1 ;;
esac

# 每条车道对应的 make 目标
targets_of() {
    case $1 in
    tests)    printf '%s' "${CHECK_LANE_TESTS:-}" ;;
    tests2)   printf '%s' "${CHECK_LANE_TESTS2:-}" ;;
    selfhost) printf '%s' "${CHECK_LANE_SELFHOST:-}" ;;
    std)      printf '%s' "${CHECK_LANE_STD:-}" ;;
    bare)     printf '%s' "${CHECK_LANE_BARE:-}" ;;
    *)        printf '' ;;
    esac
}

# 先串行把编译器建好：车道里的 make 都用 -o build 跳过了 build 这个前置目标，
# 让五条车道同时去 make -C tcc 编同一批 .o 没有意义（也容易互相踩）。
make --no-print-directory build || {
    echo "check-all: 构建失败，先修好 make build" >&2
    exit 1
}

tmp=$(mktemp -d "${TMPDIR:-/tmp}/c6-check-all.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM

# 一条车道：跑完把「退出码 用时」写进 $tmp/<lane>.rc，输出加前缀转发。
# 写文件而不是用管道状态，是因为 /bin/sh 没有 pipefail，管道的退出码是 sed 的。
run_lane() {
    lane=$1
    tgts=$(targets_of "$lane")
    start=$(date +%s)
    (
        # -j1：车道内部串行（std 家族共用 build/ 下的固定临时路径）。
        # MAKEFLAGS/MFLAGS 清空：父 make 若带 -j，jobserver 会顺着环境传进来，
        # 子 make 再被强制 -j1 就会警告「子 make 中强制 -j1: 重置 jobserver
        # 模式」。命令行变量（CFLAGS 之类）是 make 单独导出的，清空这两个不影响。
        # -o build：编译器上面已经建好了，别让每条车道再跑一遍 build
        # （那会每个目标都刷一遍 bootstrap 和 make -C tcc）。
        # shellcheck disable=SC2086
        MAKEFLAGS= MFLAGS= make --no-print-directory -j1 -o build $tgts 2>&1
        printf '%s %s\n' "$?" "$(( $(date +%s) - start ))" >"$tmp/$lane.rc"
    ) | sed -u "s/^/[$lane] /"
}

started=$(date +%s)
mode="并发"
[ "$serial" = 1 ] && mode="顺序"
echo "check-all: 五条车道${mode}跑（tests / tests2 / selfhost / std / bare）"
for lane in $lanes; do
    tgts=$(targets_of "$lane")
    if [ -z "$tgts" ]; then
        echo "check-all: 车道 $lane 的目标列表是空的（Makefile 没把 CHECK_LANE_$(echo "$lane" | tr 'a-z' 'A-Z') 传进来？）" >&2
        exit 1
    fi
done

if [ "$serial" = 1 ]; then
    for lane in $lanes; do
        run_lane "$lane"
    done
else
    for lane in $lanes; do
        run_lane "$lane" &
    done
    wait
fi

nfail=0
echo "check-all: 用时 $(( $(date +%s) - started ))s"
for lane in $lanes; do
    rc=1
    secs="?"
    if [ -f "$tmp/$lane.rc" ]; then
        read -r rc secs <"$tmp/$lane.rc"
    fi
    if [ "$rc" = 0 ]; then
        printf '  ok    %-9s %ss\n' "$lane" "$secs"
    else
        nfail=$((nfail + 1))
        printf '  FAIL  %-9s %ss\n' "$lane" "$secs"
    fi
done

if [ "$nfail" -ne 0 ]; then
    echo "check-all: $nfail 条车道失败（看上面带 [车道名] 前缀的 FAIL 行）" >&2
    exit 1
fi
echo "check-all: 五条车道全部通过"
