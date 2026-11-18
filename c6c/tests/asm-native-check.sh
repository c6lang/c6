#!/bin/sh
# native 后端的内联汇编机器码回归（asmx + 固定寄存器约束 + 系统指令编码）。
#
# 与 asmcheck.sh 的分工：asmcheck.sh 验的是 -S 文本汇编那条路，本脚本验的是
# **native 后端直接发机器码**那条路——c6c -c 出的 ET_REL .o 里必须真有
# rdtsc/wrmsr/cpuid 的字节，以及固定寄存器约束 a/b/c/d 的装值/写回。
#
# 早期这里的 bug：操作数组解析在 `::` 上卡住 → 整条 asm 静默丢弃 → .o 里
# 一条指令都没有（只剩空壳栈帧）。另一个 bug：输出约束写回有了、输入约束
# 的 mov 装值没有，wrmsr 用的是调用残值。两条都在这里锁死。
set -u

C6C=${C6C:-./build/c6c}
OBJDUMP=${OBJDUMP:-objdump}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
TMPDIR="$WORK/tmp"
export TMPDIR
mkdir -p "$TMPDIR"

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }

case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac

cat >"$WORK/ia.c" <<'EOF'
unsigned long rd(void) {
    unsigned long x;
    asm volatile("rdtsc" : "=a"(x));
    return x;
}
void wr(unsigned long lo, unsigned long hi, unsigned long m) {
    asm volatile("wrmsr" :: "c"(m), "a"(lo), "d"(hi));
}
void cp(unsigned int leaf, unsigned int *out) {
    asm volatile("cpuid"
                 : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
                 : "a"(leaf));
}
EOF

"$C6C" -c "$WORK/ia.c" -o "$WORK/ia.o" >/dev/null 2>&1 || {
    bad "汇编源码编译"; echo "native 内联汇编：通过 $pass / 失败 $fail"; exit 1;
}

# objdump 反汇编（本地化输出，避免依赖 locale 的助记符大小写）
DIS=$("$OBJDUMP" -d "$WORK/ia.o" 2>/dev/null)

# 1) 系统指令真实字节出现
echo "$DIS" | grep -qiE 'rdtsc|0f 31' && ok "rdtsc 机器码（0F 31）" \
    || bad "rdtsc 机器码缺失（整条 asm 被丢弃？）"
echo "$DIS" | grep -qiE 'wrmsr|0f 30' && ok "wrmsr 机器码（0F 30）" \
    || bad "wrmsr 机器码缺失"
echo "$DIS" | grep -qiE 'cpuid|0f a2' && ok "cpuid 机器码（0F A2）" \
    || bad "cpuid 机器码缺失"

# 2) 输入约束装值：wrmsr 前要有 3 条 mov 把帧槽装进 %rcx/%rax/%rdx
WR=$(echo "$DIS" | sed -n '/<wr>:/,/<cp>:/p')
for reg in rcx rax rdx; do
    echo "$WR" | grep -qE "mov[[:space:]].*%${reg}$" && ok "wrmsr 输入装值 %${reg}" \
        || bad "wrmsr 输入装值 %${reg} 缺失（用的是残值？）"
done

# 3) 输出约束写回：cpuid 后要有 4 条把物理寄存器写回帧槽
CP=$(echo "$DIS" | sed -n '/<cp>:/,$p')
for reg in rax rbx rcx rdx; do
    echo "$CP" | grep -qE "mov[[:space:]]+%${reg}," && ok "cpuid 输出写回 %${reg}" \
        || bad "cpuid 输出写回 %${reg} 缺失"
done

echo "native 内联汇编：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
