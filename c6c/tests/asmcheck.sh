#!/bin/sh
# -S（只出汇编文本）的回归：c6c → .s → 系统 as → 真跑，退出码要和 native 后端一致。
#
# 口径：
#   * 文本是给 GNU as 的（`.intel_syntax noprefix`），所以验收用 `cc -x assembler`
#     真汇编、真链接、真跑，不是"看着像"就算过；
#   * 每个用例都拿 native 后端（c6c -o）的结果做对照：两条路的退出码必须一样；
#   * 范围是**不依赖自带运行时的程序**（-nostdlib）：浮点、变参、内联汇编、TLS
#     的文本形式还没写，遇到会明确报错并拒绝写文件——这条也在用例里验。
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

case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac

# 一组"自己算、按退出码给答案"的小程序：每个都用 native 与 -S 两条路编，
# 退出码一致才算过。
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
    return p.x + p.y + p.z;   /* 10 + 40 + 30 = 80 */
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
    int t = pick(0) + pick(1) + pick(2) + pick(9);
    return t;                 /* 7 + 11 + 42 + 1 = 61 */
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
        bad "$t：native 对照编译" "$(tail -2 "$WORK/$t.n.log")"
        continue
    fi
    "$WORK/$t.native"
    nrc=$?

    if ! "$C6C" -S -nostdlib "$WORK/$t.c6" -o "$WORK/$t.s" >"$WORK/$t.s.log" 2>&1; then
        bad "$t：-S 编译" "$(tail -2 "$WORK/$t.s.log")"
        continue
    fi
    if ! "$CC" -x assembler -nostdlib -static -o "$WORK/$t.asm" "$WORK/$t.s" \
            >"$WORK/$t.as.log" 2>&1; then
        bad "$t：系统 as 汇编" "$(tail -2 "$WORK/$t.as.log")"
        continue
    fi
    "$WORK/$t.asm"
    arc=$?
    if [ "$arc" = "$nrc" ]; then
        ok "$t：-S 出的汇编真跑出 $arc（与 native 一致）"
    else
        bad "$t：结果" "native=$nrc，asm=$arc"
    fi
done

# --- 文本本身的形状 ---
printf '%s\n' "$(head -1 "$WORK/loop.s" 2>/dev/null)" | grep -q "intel_syntax" &&
    ok ".s 开头声明了 .intel_syntax noprefix（GNU as 才按 Intel 语法读）" ||
    bad ".s 的语法声明" "$(head -1 "$WORK/loop.s" 2>/dev/null)"
grep -q "^_start:" "$WORK/loop.s" 2>/dev/null &&
    ok ".s 里带自己的 _start（不依赖 crt）" ||
    bad ".s 里的 _start" "没找到"
grep -q "^main:" "$WORK/loop.s" 2>/dev/null &&
    ok ".s 里函数符号就是 C 名（main:）" ||
    bad ".s 里的函数符号" "没找到 main:"

# --- 不支持的东西要明确报错、且不写文件 ---
cat >"$WORK/var.c6" <<'EOF'
int printf(const char *fmt, ...);
int main(void) { printf("hi\n"); return 0; }
EOF
if "$C6C" -S "$WORK/var.c6" -o "$WORK/var.s" >"$WORK/var.log" 2>"$WORK/var.err"; then
    bad "-S 遇到变参/浮点应当拒绝" "居然成功写出来了"
else
    if grep -q "暂不支持" "$WORK/var.err" && [ ! -f "$WORK/var.s" ]; then
        ok "-S 遇到没实现的 op 会点名报错且不写文件（不做静默错产物）"
    else
        bad "-S 的拒绝方式" "stderr『$(head -1 "$WORK/var.err")』，产物$( [ -f "$WORK/var.s" ] && echo 存在 || echo 不存在)"
    fi
fi

echo "c6c -S 回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
