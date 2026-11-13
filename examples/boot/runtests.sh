#!/bin/sh
# 把 tests/*.c6 里的 test 块拿到**裸机**上跑：每个测试文件配一张自动生成的
# 入口（harness.py）、一层 libc 替身（shim.c）和两个 outb（io.c），由
# boot.S 那个 512 字节引导器拉起来，qemu-system-x86_64 当空机器。
#
#   make fstest-bare-tests          # 有 KVM 用 KVM
#   NO_KVM=1 make fstest-bare-tests # 纯软件模拟
#   BARE_BUILD=build/bare/tests examples/boot/runtests.sh  # 换产物目录（默认 build/）
#
# 跳过的几个：它们要的东西裸机上真没有（不是运行时不行，是测试自己要用）
#   gc_drop gc_thread thread thread_handle tuple_thread    spawn -> pthread_create
#   defer_thread                                           -> clone/futex
#   method                                                 libc 的 FILE 布局
#   async_highfd                                           dup2/getrlimit/setrlimit
#
# 整个文件跳太重的话，可以只跳一个 test 块：在块上一行写 `// BARE-SKIP`
# （harness.py 认这个标记，块号不变），例如 tests/generic_fn.c6 里起线程那块。
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
build=${BARE_BUILD:-build}
mkdir -p "$build"

skip=" gc_drop gc_thread thread thread_handle tuple_thread defer_thread method async_highfd "
pass=0
fail=0
failed=""
skipped=""
block_skipped=""

for src in tests/*.c6; do
    name=$(basename "$src" .c6)
    case "$skip" in
    *" $name "*) skipped="$skipped $name"; continue ;;
    esac

    tu=""
    hsrcs="$src"
    one=""
    if [ -e "tests/tu/$name.c6" ]; then
        tu="tests/tu/$name.c6"
        # tu 里也有 test 块时，两边的编号是"整次编译连续"的，只能一口气编
        if grep -q '^test ' "$tu" 2>/dev/null; then
            one=1
            hsrcs="$src $tu"
        fi
    fi

    if ! hinfo=$(python3 examples/boot/harness.py "$build/harness_$name.c" $hsrcs); then
        fail=$((fail + 1))
        failed="$failed $name"
        echo "FAIL $name: 生成 harness 失败"
        continue
    fi
    # harness.py 报 "跑几块 跳过几块"：块级跳过只影响这一个文件，写进 ok 行
    nblock=$(echo "$hinfo" | awk '{print $2}')
    note=""
    if [ "${nblock:-0}" -gt 0 ]; then
        note="（块级跳过 $nblock）"
        block_skipped="$block_skipped $name:$nblock"
    fi

    if out=$(ONE_TU=$one BARE_BUILD="$build" \
             examples/boot/build.sh "$src" $tu examples/boot/shim.plain.c \
             examples/boot/io.c "$build/harness_$name.c" 2>&1); then
        line=$(echo "$out" | grep 'test(s),' | tail -1)
        pass=$((pass + 1))
        echo "ok   $name: $line$note"
    else
        fail=$((fail + 1))
        failed="$failed $name"
        echo "FAIL $name"
        echo "$out" | grep -E 'FAIL:|error:|unresolved' | head -3 | sed 's/^/      /'
    fi
done

echo "bare tests: $pass 通过, $fail 失败（跳过:$skipped）${block_skipped:+（块级跳过:$block_skipped）}"
[ "$fail" -eq 0 ] || {
    echo "失败:$failed" >&2
    exit 1
}
