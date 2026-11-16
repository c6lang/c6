#!/bin/sh
# ctu 后端的 rtos-c6 冒烟：把 kernel/*.c6 打成 per-TU 的 .c，交给 xtensa gcc 收下。
#
# 口径（--emit-ctu）：**per-TU 预处理后的 C 文本**——一个输入一个 .c、真 C ABI、
# 不要 main、不带 c6c 自己的运行时。rtos-c6 的 kernel/*.c6 本来就是纯 C（没有
# fn/let/defer/切片/泛型/async 这些扩展），所以对它们这份后端基本是恒等翻译。
#
# 这条脚本只量"交出去的东西能不能被**目标**编译器当成 C 收下"：
#
#   c6c --emit-ctu -o $T/ctu/ ... kernel/os.c6   →  $T/ctu/os.c
#   xtensa-esp32s3-elf-gcc ... -c $T/ctu/os.c    →  $T/obj/os.o
#
# 它量不到链接、也量不到跑起来：这一步只证明 ABI/语法/类型对得上（真 C 的
# 形参、返回值、指针、全局量），后面的事归 rtos-c6 自己的构建。
#
# 用法：
#   C6C=<path to c6c> sh c6c/tests/ctu/rtos-smoke.sh
#   RTOS=~/rtos-c6 XTP=~/.../xtensa-esp32s3-elf- sh c6c/tests/ctu/rtos-smoke.sh
#
#   C6C   必填，c6c 可执行文件
#   RTOS  rtos-c6 仓库根（默认 $HOME/rtos-c6）
#   XTP   xtensa 工具链的**前缀**（默认
#         $HOME/.c6os/toolchain/xtensa-esp-elf/bin/xtensa-esp32s3-elf-）
#
# 退出码：0 = 通过（环境不全时也返回 0，打印"跳过"）；1 = 有 TU 编不过；
#         2 = 用法错误（没给 C6C / C6C 不可执行 / 建不了临时目录）。

set -u

DIR=$(cd "$(dirname "$0")" && pwd)
# 仓库根：`-B tcc` 是相对它写的（tcc/ 在仓库根下），所以 c6c 一律在根目录里跑
ROOT=$(cd "$DIR/../../.." && pwd)

C6C=${C6C:-}
RTOS=${RTOS:-$HOME/rtos-c6}
XTP=${XTP:-$HOME/.c6os/toolchain/xtensa-esp-elf/bin/xtensa-esp32s3-elf-}

if [ -z "$C6C" ]; then
    echo "rtos-smoke: 需要 C6C=<c6c 可执行>（C6C=$C6C）" >&2
    exit 2
fi
# 和 tests/ctu/run.sh 同一个口径：C6C 先归一成绝对路径，再拿它编
case $C6C in
    /*) ;;
    *) C6C=$(cd "$(dirname "$C6C")" 2>/dev/null && pwd)/$(basename "$C6C") ;;
esac
if [ ! -x "$C6C" ]; then
    echo "rtos-smoke: $C6C 不存在或不可执行" >&2
    exit 2
fi

if [ ! -d "$RTOS" ] || [ ! -x "${XTP}gcc" ]; then
    echo "rtos-smoke: 跳过（缺 xtensa 工具链或 rtos-c6）"
    exit 0
fi

RTOS=$(cd "$RTOS" && pwd)

T=$(mktemp -d "${TMPDIR:-/tmp}/c6c-rtos-smoke.XXXXXX") || {
    echo "rtos-smoke: 建不了临时目录" >&2
    exit 2
}
trap 'rm -rf "$T"' EXIT INT TERM HUP

mkdir -p "$T/ctu" "$T/obj"

# 按依赖顺序列（哪个存在测哪个）
TUS="os panic log tick timer timeout task heap"

N_OK=0
N_FAIL=0
N_KNOWN=0
N_SEEN=0
FAILED=""
KNOWNED=""

for b in $TUS; do
    f="$RTOS/kernel/$b.c6"
    [ -f "$f" ] || continue
    N_SEEN=$((N_SEEN + 1))

    # --- 1) c6c：一个 TU 一个 .c（-o 给目录，文件名按输入 basename） ---
    if ! ( cd "$ROOT" && "$C6C" --emit-ctu -o "$T/ctu/" -B tcc \
            -I "$RTOS/include" -I "$RTOS" "$f" ) >"$T/c6c.$b.log" 2>&1; then
        N_FAIL=$((N_FAIL + 1))
        FAILED="$FAILED $b"
        echo "rtos-smoke: FAIL $b.c6（c6c --emit-ctu）"
        head -3 "$T/c6c.$b.log" | sed 's/^/    /'
        continue
    fi
    if [ ! -f "$T/ctu/$b.c" ]; then
        N_FAIL=$((N_FAIL + 1))
        FAILED="$FAILED $b"
        echo "rtos-smoke: FAIL $b.c6（没产出 $T/ctu/$b.c）"
        continue
    fi

    # --- 2) xtensa gcc：把那份 .c 当 C 编成目标文件 ---
    if ( cd "$ROOT" && "${XTP}gcc" -mabi=call0 -Os -ffreestanding -fno-builtin \
            -fno-common -mlongcalls -mtext-section-literals \
            -x c -c "$T/ctu/$b.c" -o "$T/obj/$b.o" ) >"$T/gcc.$b.log" 2>&1; then
        N_OK=$((N_OK + 1))
        echo "rtos-smoke: OK   $b.c → $b.o"
        continue
    fi

    # task.c6 现在会因为 `port_cur_ctx` 隐式声明而编不过（rtos-c6 源码的宽松口径
    # 惯出来的，和这个后端无关）。只在**确实是这条**的时候记成已知失败。
    if [ "$b" = "task" ] && grep -q 'port_cur_ctx' "$T/gcc.$b.log" 2>/dev/null; then
        N_KNOWN=$((N_KNOWN + 1))
        KNOWNED="$KNOWNED $b"
        echo "rtos-smoke: 已知 $b.c6（port_cur_ctx 隐式声明，rtos-c6 源码问题，不计失败）"
        continue
    fi

    N_FAIL=$((N_FAIL + 1))
    FAILED="$FAILED $b"
    echo "rtos-smoke: FAIL $b.c6（xtensa gcc）"
    if grep -q -i 'error' "$T/gcc.$b.log" 2>/dev/null; then
        grep -i 'error' "$T/gcc.$b.log" | head -3 | sed 's/^/    /'
    else
        head -3 "$T/gcc.$b.log" | sed 's/^/    /'
    fi
done

if [ "$N_SEEN" -eq 0 ]; then
    echo "rtos-smoke: 跳过（$RTOS/kernel 下没有 os/panic/log/tick/timer/timeout/task/heap.c6）"
    exit 0
fi

echo "rtos-smoke: 编译通过 $N_OK / 失败 $N_FAIL"
if [ "$N_KNOWN" -gt 0 ]; then
    echo "rtos-smoke: 另有 $N_KNOWN 个已知失败（$KNOWNED ）—— 不算失败"
fi
if [ "$N_FAIL" -gt 0 ]; then
    echo "rtos-smoke: 失败的 TU：$FAILED"
    exit 1
fi
exit 0
