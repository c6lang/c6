#!/bin/sh
# x86-64 之外的代码生成：c6c 的 IR 经 LLVM 落到 aarch64，在 qemu 上真跑。
#
# 口径（重要，别把它读成"c6c 有 aarch64 native 后端"）：
#   * c6c 自己的 native 后端只出 x86-64 机器码；
#   * 但 IR 是与目标无关的，`--emit-llvm` 出来的 .ll 交给 `llc -march=aarch64`
#     就能出 aarch64 汇编/目标文件——这条链在这里被**真跑**验过：aarch64 ELF
#     在 qemu-aarch64 上跑出和 x86-64 一样的结果；
#   * 纯计算、-nostdlib：运行时（rt.c6）里的跳板是 x86-64 内联汇编，跨目标
#     过不去，所以这条用例只用"自己算、不碰 libc"的程序（module asm 行过滤掉）。
set -u

C6C=${C6C:-./build/c6c}
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

LLC=${LLC:-}
if [ -z "$LLC" ]; then
    for c in /usr/lib/llvm-19/bin/llc /usr/lib/llvm-18/bin/llc /usr/lib/llvm-17/bin/llc; do
        [ -x "$c" ] && LLC=$c && break
    done
    [ -n "$LLC" ] || LLC=$(command -v llc 2>/dev/null || true)
fi

for t in clang ld.lld qemu-aarch64; do
    if ! have "$t"; then
        echo "c6c 跨目标回归：跳过（没有 $t）"
        exit 0
    fi
done
if [ -z "$LLC" ] || [ ! -x "$LLC" ]; then
    echo "c6c 跨目标回归：跳过（没有 llc）"
    exit 0
fi
if ! "$LLC" --version 2>/dev/null | grep -q aarch64; then
    echo "c6c 跨目标回归：跳过（这个 llc 没编进 aarch64 目标）"
    exit 0
fi

# 纯计算：算 1..6 的平方和 = 91，退出码就是它
cat >"$WORK/calc.c6" <<'EOF'
int main(void)
{
    int s = 0;
    int i = 0;

    for (i = 1; i <= 6; i++)
        s = s + i * i;
    return s;
}
EOF

# --- 1. x86-64（c6c 自己的后端）跑出 91，作为对照 ---
if "$C6C" -o "$WORK/x86" "$WORK/calc.c6" >/dev/null 2>&1 && [ -x "$WORK/x86" ]; then
    "$WORK/x86"
    rc=$?
    if [ "$rc" = 91 ]; then
        ok "x86-64（c6c native 后端）：退出码 91"
    else
        bad "x86-64 对照" "退出码 $rc（应为 91）"
    fi
else
    bad "x86-64 对照编译" "编不出来"
fi

# --- 2. IR → aarch64 汇编文本（先证明真的换了架构） ---
"$C6C" --emit-llvm -nostdlib "$WORK/calc.c6" -o "$WORK/calc.ll" >"$WORK/ll.log" 2>&1
# 运行时跳板是 x86-64 内联汇编，跨目标过不去：这条用例不看它们
grep -v '^module asm' "$WORK/calc.ll" >"$WORK/calc.pure.ll"

if "$LLC" -march=aarch64 -o "$WORK/calc.s" "$WORK/calc.pure.ll" >"$WORK/llc-s.log" 2>&1; then
    if grep -q "main:" "$WORK/calc.s" &&
            grep -qE '\b(madd|mul|add|b|cbz|cmp)\b' "$WORK/calc.s" &&
            ! grep -q "%rax" "$WORK/calc.s"; then
        ok "IR → aarch64 汇编（有 main: 与 aarch64 指令，没有 x86 寄存器）"
    else
        bad "aarch64 汇编内容" "$(head -3 "$WORK/calc.s" | tr '\n' ' ')"
    fi
else
    bad "llc -march=aarch64" "$(head -3 "$WORK/llc-s.log")"
fi

# --- 3. 落成 aarch64 目标文件 + 自己写 _start + ld.lld 链接 + qemu 真跑 ---
printf '    .text\n    .globl _start\n_start:\n    bl    main\n    mov   w0, w0\n    mov   x8, #93\n    svc   #0\n' \
    >"$WORK/start.s"
# llc 会把 IR 里的 zeroinitializer 变成 memset 调用：宿主那一侧要补一个
# （真实工程里这是 libc 的事，这里为了"跑起来"手写一个 aarch64 版本）
printf '    .text\n    .globl memset\nmemset:\n    mov   x3, x0\n    mov   w4, w1\n    mov   x5, #0\n1:  cmp   x5, x2\n    b.hs  2f\n    strb  w4, [x3, x5]\n    add   x5, x5, #1\n    b     1b\n2:  ret\n' \
    >"$WORK/mem.s"
if clang --target=aarch64-linux-gnu -c "$WORK/start.s" -o "$WORK/start.o" \
        >"$WORK/clang.log" 2>&1 &&
        clang --target=aarch64-linux-gnu -c "$WORK/mem.s" -o "$WORK/mem.o" \
        >>"$WORK/clang.log" 2>&1 &&
        "$LLC" -march=aarch64 -filetype=obj -o "$WORK/calc.o" "$WORK/calc.pure.ll" \
        >"$WORK/llc-o.log" 2>&1 &&
        ld.lld -m aarch64elf -e _start -o "$WORK/calc.elf" "$WORK/start.o" \
        "$WORK/mem.o" "$WORK/calc.o" \
        >"$WORK/ld.log" 2>&1; then
    if file "$WORK/calc.elf" 2>/dev/null | grep -q "ARM aarch64"; then
        ok "产物是 ARM aarch64 ELF（clang 汇编 _start + llc 出目标文件 + ld.lld 链接）"
    else
        bad "产物架构" "$(file "$WORK/calc.elf" 2>/dev/null | cut -c1-80)"
    fi
    qemu-aarch64 "$WORK/calc.elf" >"$WORK/qemu.out" 2>"$WORK/qemu.err"
    rc=$?
    if [ "$rc" = 91 ]; then
        ok "qemu-aarch64 真跑：退出码 91（与 x86-64 后端一致）"
    else
        bad "qemu-aarch64 运行" "退出码 $rc，stderr『$(head -2 "$WORK/qemu.err" | tr '\n' ' ')』"
    fi
else
    bad "aarch64 链接" "$(tail -2 "$WORK/clang.log" "$WORK/llc-o.log" "$WORK/ld.log" 2>/dev/null | tr '\n' ' ')"
fi

echo "c6c 跨目标回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
