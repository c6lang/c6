#!/bin/sh
# stage1 自编回归：**c6c 必须能编自己**（不是"tcc 能编 c6c"）。
#
# 为什么单列一条：引导链是 tcc/c6（C89 口径）→ build/c6c → selfhost。
# 第一步我们用的是 tcc，它替 c6c 遮掉了一整类问题——**隐式函数声明**。
# 实测踩到的样子：c6c/src/src.c6 用 memchr()，c6c/include/string.h 里没有声明、
# lib/rt.c6 里只有定义（而 lib/rt.c6 是编用户程序时才挂上去的运行时）。
# tcc 按 C89 收下隐式声明、照样出体；轮到 c6c 编自己，就报
#   未声明的标识符 'memchr'
# 但 selfhost 目标只比"c6c2 == c6c3"，stage1 本身怎么来的没人验——于是一个
# 只在 C89 下成立的源码，要到很久以后才炸。
#
# 口径：c6c 不带任何运行时（就是 stage1 的构建形态）**直接**编 c6c.c6，
# 要求前端零诊断、退出码 0、产物是 ET_REL。编得过是硬要求；编出的 .o 能不能
# 由我们的链接器接住，是隔壁 ldcheck 的事，这里不重复。
set -u

C6C=${C6C:-./build/c6c}
SRC=${SRC:-./src/c6c.c6}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }

# --- 1. 前端零诊断：只做语法/语义，不要求出码 ---
"$C6C" -fsyntax-only "$SRC" >"$WORK/syn.log" 2>&1
rc=$?
if [ "$rc" -eq 0 ] && [ ! -s "$WORK/syn.log" ]; then
    ok "-fsyntax-only 编自己零诊断"
else
    bad "-fsyntax-only 编自己零诊断" "退出码 $rc：$(head -5 "$WORK/syn.log")"
fi

# --- 2. 真出码：stage1 的构建形态（不带运行时）编出 ET_REL ---
"$C6C" -c "$SRC" -o "$WORK/self.o" >"$WORK/obj.log" 2>&1
rc=$?
if [ "$rc" -eq 0 ] && [ -s "$WORK/self.o" ]; then
    ok "-c 出目标文件（退出码 0）"
else
    bad "-c 出目标文件（退出码 0）" "退出码 $rc：$(head -5 "$WORK/obj.log")"
fi

# --- 3. 产物必须是 ET_REL、且有代码段（防止"空着也退出 0"） ---
if [ -s "$WORK/self.o" ] && command -v readelf >/dev/null 2>&1; then
    if readelf -h "$WORK/self.o" 2>/dev/null | grep -q 'REL (可重定位文件\|REL (Relocatable'; then
        ok "产物是 ET_REL"
    else
        bad "产物是 ET_REL" "$(readelf -h "$WORK/self.o" 2>/dev/null | grep Type)"
    fi
    if readelf -S "$WORK/self.o" 2>/dev/null | grep -q '\.text'; then
        ok "产物有 .text"
    else
        bad "产物有 .text"
    fi
else
    echo "skip 产物结构检查（没编出来或没有 readelf）"
fi

echo "stage1 自编回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
