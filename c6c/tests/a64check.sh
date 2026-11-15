#!/bin/sh
# c6c 的第二套 native 后端（aarch64）的回归：c6c 自己选指令 → aarch64 汇编文本
# → clang 汇编 → ld.lld 链接 → qemu-aarch64 真跑，结果要和 x86-64 后端一致。
#
# 口径：
#   * 指令是 c6c 选的（IR → aarch64），不是把 IR 丢给 LLVM；汇编器只负责编码；
#   * 每个用例都和 x86-64 native 后端的退出码对照；
#   * 范围同 -S：纯整数/指针程序（-nostdlib）；浮点/变参/VLA/内联汇编会明确报错。
set -u

C6C=${C6C:-./build/c6c}
CC=${CC:-cc}
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

for t in clang ld.lld qemu-aarch64; do
    if ! have "$t"; then
        echo "c6c aarch64 后端回归：跳过（没有 $t）"
        exit 0
    fi
done

mk() {
    name=$1
    cat >"$WORK/$name.c6"
}

mk loop <<'EOF'
int main(void)
{
    int i = 0;
    int s = 0;

    for (i = 1; i <= 5; i++)
        s = s + i * i;
    return s;                 /* 55 */
}
EOF

mk call <<'EOF'
int add(int a, int b) { return a + b; }

int main(void)
{
    return add(2, 3) * 10;    /* 50 */
}
EOF

mk array <<'EOF'
int add(int a, int b) { return a + b; }

int main(void)
{
    int a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int i = 0;
    int s = 0;

    for (i = 0; i < 8; i++)
        s = add(s, a[i]);
    return s;                 /* 36 */
}
EOF

mk rec <<'EOF'
int fact(int n)
{
    if (n <= 1)
        return 1;
    return n * fact(n - 1);
}

int main(void)
{
    return fact(5);           /* 120 */
}
EOF

mk stru <<'EOF'
struct P { int x; int y; int z; };

int main(void)
{
    struct P p = {10, 20, 30};
    struct P *q = &p;

    q->y = q->x + q->z;       /* 40 */
    return p.x + p.y + p.z;   /* 80 */
}
EOF

mk sw <<'EOF'
int pick(int k)
{
    switch (k) {
    case 0: return 7;
    case 1: return 11;
    case 2: return 42;
    default: return 1;
    }
}

int main(void)
{
    return pick(0) + pick(1) + pick(2) + pick(9);   /* 61 */
}
EOF

mk glob <<'EOF'
int g = 30;
static int h = 12;

int main(void)
{
    g = g + h;
    return g;                 /* 42 */
}
EOF

mk bits <<'EOF'
int main(void)
{
    unsigned int v = 0xF0u;
    int s = 0;

    v = v | 0x0Fu;
    v = v << 1;
    v = v >> 2;
    v = v ^ 0x3Cu;
    v = v & 0xFFu;
    s = (int)v;
    if (s > 100)
        s = s - 100;
    return s;                 /* 0x43 = 67 */
}
EOF

for t in loop call array rec stru sw glob bits; do
    if ! "$C6C" -nostdlib -o "$WORK/$t.native" "$WORK/$t.c6" >"$WORK/$t.n.log" 2>&1; then
        bad "$t：x86-64 对照编译" "$(tail -2 "$WORK/$t.n.log")"
        continue
    fi
    "$WORK/$t.native"
    nrc=$?

    if ! "$C6C" -S --target=aarch64 -nostdlib "$WORK/$t.c6" -o "$WORK/$t.s" \
            >"$WORK/$t.s.log" 2>&1; then
        bad "$t：aarch64 出文本" "$(tail -2 "$WORK/$t.s.log")"
        continue
    fi
    if ! clang --target=aarch64-linux-gnu -c "$WORK/$t.s" -o "$WORK/$t.o" \
            >"$WORK/$t.as.log" 2>&1; then
        bad "$t：clang 汇编" "$(tail -2 "$WORK/$t.as.log")"
        continue
    fi
    if ! ld.lld -m aarch64elf -e _start -o "$WORK/$t.elf" "$WORK/$t.o" \
            >"$WORK/$t.ld.log" 2>&1; then
        bad "$t：ld.lld 链接" "$(tail -2 "$WORK/$t.ld.log")"
        continue
    fi
    qemu-aarch64 "$WORK/$t.elf" >/dev/null 2>"$WORK/$t.run.log"
    arc=$?
    if [ "$arc" = "$nrc" ]; then
        ok "$t：aarch64 上跑出 $arc（与 x86-64 一致）"
    else
        bad "$t：结果" "x86-64=$nrc，aarch64=$arc"
    fi
done

# --- 文本确实是 aarch64，不是 x86-64 换个名字 ---
s="$WORK/loop.s"
if grep -qE '\b(ldr|str|movz|cbz|cbnz|madd|sdiv|msub)\b' "$s" &&
        ! grep -q '%r' "$s" && ! grep -q '%e' "$s"; then
    ok "汇编里是 aarch64 指令（ldr/str/movz/cbz…），没有 x86 寄存器"
else
    bad "aarch64 指令" "$(grep -m2 -E 'ldr|mov' "$s" | tr '\n' ' ')"
fi
grep -q "^_start:" "$s" && grep -q "svc" "$s" &&
    ok "_start 用 svc #0 退出（aarch64 的 syscall 约定）" ||
    bad "_start 的 aarch64 形式" "没找到 _start/svc"

# --- 没实现的东西要拒绝，而且要点名是 aarch64 后端 ---
cat >"$WORK/var.c6" <<'EOF'
int printf(const char *fmt, ...);
int main(void) { printf("hi\n"); return 0; }
EOF
if "$C6C" -S --target=aarch64 "$WORK/var.c6" -o "$WORK/var.s" >/dev/null 2>"$WORK/var.err"; then
    bad "aarch64 后端遇到变参应当拒绝" "居然成功写出来了"
else
    grep -q "aarch64 后端暂不支持" "$WORK/var.err" && [ ! -f "$WORK/var.s" ] &&
        ok "aarch64 后端遇到没实现的 op 点名报错且不写文件" ||
        bad "aarch64 的拒绝方式" "$(head -1 "$WORK/var.err")"
fi

echo "c6c aarch64 后端回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
