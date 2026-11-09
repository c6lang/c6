#!/bin/sh
# 把 tests/*.c 里的 test 块拿到**裸机**上跑：每个测试文件配一张自动生成的
# 入口（harness.py）、一层 libc 替身（shim.c）和两个 outb（io.c），由
# boot.S 那个 512 字节引导器拉起来，qemu-system-x86_64 当空机器。
#
#   make fstest-bare-tests          # 有 KVM 用 KVM
#   NO_KVM=1 make fstest-bare-tests # 纯软件模拟
#
# 跳过的几个：它们要的东西裸机上真没有（不是运行时不行，是测试自己要用）
#   gc_drop gc_thread thread thread_handle tuple_thread   go -> pthread_create
#                                                         -> clone/futex
#   method                                                libc 的 FILE 布局
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
build=build
mkdir -p "$build"

skip=" gc_drop gc_thread thread thread_handle tuple_thread method "
pass=0
fail=0
failed=""
skipped=""

for src in tests/*.c; do
    name=$(basename "$src" .c)
    case "$skip" in
    *" $name "*) skipped="$skipped $name"; continue ;;
    esac

    tu=""
    hsrcs="$src"
    one=""
    if [ -e "tests/tu/$name.c" ]; then
        tu="tests/tu/$name.c"
        # tu 里也有 test 块时，两边的编号是"整次编译连续"的，只能一口气编
        if grep -q '^test ' "$tu" 2>/dev/null; then
            one=1
            hsrcs="$src $tu"
        fi
    fi

    if ! python3 examples/boot/harness.py "$build/harness_$name.c" $hsrcs >/dev/null 2>&1; then
        fail=$((fail + 1))
        failed="$failed $name"
        echo "FAIL $name: 生成 harness 失败"
        continue
    fi

    if out=$(ONE_TU=$one examples/boot/build.sh "$src" $tu examples/boot/shim.plain.c \
             examples/boot/io.c "$build/harness_$name.c" 2>&1); then
        line=$(echo "$out" | grep 'test(s),' | tail -1)
        pass=$((pass + 1))
        echo "ok   $name: $line"
    else
        fail=$((fail + 1))
        failed="$failed $name"
        echo "FAIL $name"
        echo "$out" | grep -E 'FAIL:|error:|unresolved' | head -3 | sed 's/^/      /'
    fi
done

echo "bare tests: $pass 通过, $fail 失败（跳过:$skipped）"
[ "$fail" -eq 0 ] || {
    echo "失败:$failed" >&2
    exit 1
}
