#!/bin/sh
# `c6c -c` 的回归：它出的是**可重定位目标文件**（ET_REL），不是可执行文件。
#
# 口径：一个 .o 得能被系统的链接器接住——单 TU、多 TU 两种情形都要真编真跑；
# 产物结构（ELF 类型、段表、重定位、符号绑定）按 readelf 的实际输出核对，
# 免得"看起来能跑"掩盖了"其实是把地址拍死在 .o 里"。
#
# 为什么用系统 cc 当链接器：这一步验的是**目标文件**的契约（谁链都该成立），
# 用 gcc/ld 反而比用我们自己的链接器更有说服力。等 c6c 自己的链接器落地，
# 这里再加一条"我们的链接器也要接住自己出的 .o"。
set -u

C6C=${C6C:-./build/c6c}
CC=${CC:-cc}
# 有一个子检查要先 cd 到别处（默认输出名），所以这里把 C6C 变成绝对路径
case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac
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

# --- 1. 单 TU：全局变量 / 字符串字面量 / 调外部函数，再让系统链接器链起来 ---
cat >"$WORK/one.c6" <<'EOF'
int printf(const char *fmt, ...);

int g = 42;
const char *msg = "hi from object";

int add(int x, int y) { return x + y; }

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("g=%d add=%d %s\n", g, add(2, 3), msg);
    return 0;
}
EOF

if ! "$C6C" -c "$WORK/one.c6" -o "$WORK/one.o" >"$WORK/one.log" 2>&1; then
    bad "单 TU 出 .o" "$(cat "$WORK/one.log")"
else
    ok "单 TU 出 .o"
    if "$CC" "$WORK/one.o" -o "$WORK/one" >"$WORK/one.link" 2>&1; then
        got=$("$WORK/one")
        if [ "$got" = "g=42 add=5 hi from object" ]; then
            ok "单 TU：系统链接器接住 .o 并跑对"
        else
            bad "单 TU 跑出来的输出" "『$got』"
        fi
    else
        bad "单 TU：系统链接器接住 .o" "$(cat "$WORK/one.link")"
    fi
fi

# --- 2. 多 TU：库 .o 定义全局，主 .o 用 extern 引用（跨 TU 数据 + 函数） ---
cat >"$WORK/lib.c6" <<'EOF'
int counter = 7;
int lib_add(int a, int b) { return a + b + counter; }
EOF
cat >"$WORK/prog.c6" <<'EOF'
int printf(const char *fmt, ...);
extern int counter;
int lib_add(int a, int b);

int main(void)
{
    counter = counter + 1;
    printf("counter=%d lib_add=%d\n", counter, lib_add(10, 20));
    return 0;
}
EOF

if "$C6C" -c "$WORK/lib.c6" -o "$WORK/lib.o" >"$WORK/lib.log" 2>&1 \
   && "$C6C" -c "$WORK/prog.c6" -o "$WORK/prog.o" >"$WORK/prog.log" 2>&1; then
    # 顺序反过来也要成立：链接器按符号解析，不按 .o 的先后
    if "$CC" "$WORK/prog.o" "$WORK/lib.o" -o "$WORK/prog" >"$WORK/prog.link" 2>&1; then
        got=$("$WORK/prog")
        if [ "$got" = "counter=8 lib_add=38" ]; then
            ok "多 TU：两个 .o 链接后跨 TU 全局可读可写"
        else
            bad "多 TU 跑出来的输出" "『$got』"
        fi
    else
        bad "多 TU 链接" "$(cat "$WORK/prog.link")"
    fi
else
    bad "多 TU 出 .o" "$(cat "$WORK/lib.log" "$WORK/prog.log" 2>/dev/null)"
fi

# --- 3. 产物结构：ET_REL + 段表 + 重定位 + 符号绑定 ---
if have readelf && [ -f "$WORK/prog.o" ]; then
    if readelf -h "$WORK/prog.o" | grep -q 'REL (可重定位文件)\|REL (Relocatable file)'; then
        ok "ELF 类型是 ET_REL"
    else
        bad "ELF 类型是 ET_REL" "$(readelf -h "$WORK/prog.o" | grep '类型')"
    fi
    secs=$(readelf -S "$WORK/prog.o")
    miss=""
    for s in .text .data .rela.text .symtab .strtab .shstrtab; do
        echo "$secs" | grep -q " $s " || miss="$miss $s"
    done
    if [ -z "$miss" ]; then
        ok "段表齐全（.text/.data/.rela.*/.symtab/.strtab）"
    else
        bad "段表齐全" "缺$miss"
    fi
    # 外部调用（printf）必须是 PLT32，且符号是 UND（没被拍死成绝对地址）
    if readelf -r "$WORK/one.o" | grep -q 'R_X86_64_PLT32.*printf'; then
        ok "外部调用落成 R_X86_64_PLT32 + UND 符号"
    else
        bad "外部调用落成 R_X86_64_PLT32 + UND 符号" \
            "$(readelf -r "$WORK/one.o" 2>/dev/null | head -4)"
    fi
    # 数据段里指向 .text/.data 的地址必须是重定位，不能是常量
    if readelf -r "$WORK/one.o" | grep -q '\.rela.data' \
       && readelf -r "$WORK/one.o" | grep -q 'R_X86_64_64'; then
        ok "数据段里的地址落成 R_X86_64_64"
    else
        bad "数据段里的地址落成 R_X86_64_64" "$(readelf -r "$WORK/one.o" | tail -3)"
    fi
    # TU 私有的东西（字符串字面量、编译器内部记账格）必须是 LOCAL
    if readelf -s "$WORK/prog.o" | grep -q 'LOCAL.*\.LC0'; then
        ok "字符串字面量符号是 LOCAL"
    else
        bad "字符串字面量符号是 LOCAL" "$(readelf -s "$WORK/prog.o" | grep LC | head -3)"
    fi
    if readelf -s "$WORK/prog.o" | grep '__c6_tls_' | grep -q GLOBAL; then
        bad "TLS 记账格不该是 GLOBAL" "$(readelf -s "$WORK/prog.o" | grep __c6_tls_)"
    else
        ok "TLS 记账格不是 GLOBAL（多 .o 不冲突）"
    fi
else
    echo "skip 产物结构检查（没有 readelf）"
fi

# --- 4. 不给 -o 时默认名：src/foo.c6 → foo.o（和 gcc 一个习惯） ---
mkdir -p "$WORK/sub"
cp "$WORK/lib.c6" "$WORK/sub/foo.c6"
( cd "$WORK" && "$C6C" -c sub/foo.c6 >/dev/null 2>"$WORK/dflt.err" )
if [ -f "$WORK/foo.o" ]; then
    ok "-c 不给 -o 时输出 foo.o"
else
    bad "-c 不给 -o 时输出 foo.o" "$(cat "$WORK/dflt.err")"
fi

# --- 5. 一次编多个源文件要如实拒绝（一个 .o 对应一个 TU） ---
"$C6C" -c "$WORK/lib.c6" "$WORK/prog.c6" -o "$WORK/two.o" >"$WORK/two.log" 2>&1
rc=$?
if [ "$rc" = 2 ] && grep -q '一次只编一个源文件' "$WORK/two.log"; then
    ok "-c 多输入以退出码 2 拒绝"
else
    bad "-c 多输入以退出码 2 拒绝" "退出码 $rc：$(cat "$WORK/two.log")"
fi

echo "c6c -c（ET_REL）回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
