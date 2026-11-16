#!/bin/sh
# c6c 自己的静态链接器（src/ld.c6）的回归：`c6c -o prog a.o b.o`。
#
# 口径：
#   * 纯自足（不碰 libc）两个 .o：真链、真跑，退出码就是断言；
#   * 真实规模：自带运行时 rt.o + 一个用 printf 的程序，564 条重定位全部
#     应用，输出逐字对、退出码对——这条要 -Wl,--allow-undefined，因为 rt.o
#     里有 libc 名字的未定义符号（接 libc 是"动态链接"那个缺口的事）；
#   * 构造函数（.init_array）必须在 main 之前跑完：靠一个 constructor 写全局、
#     main 里断言的用例验，TLS 那条也走同一张表；
#   * 出错要如实报：缺符号退出码 2 且名字在 stderr；两个 .o 都带 TLS 也要拒绝。
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
# 自带运行时的源码要从 c6c 目录里找（脚本按 `cmake --build build --target ldcheck` 跑）
HERE=$(cd "$(dirname "$0")/.." && pwd)
RT="$HERE/lib/rt.c6"

# --- 1. 纯自足：两个 .o，跨 TU 全局 + 函数，退出码是断言 ---
cat >"$WORK/a.c6" <<'EOF'
int g = 42;
int add(int a, int b) { return a + b + g; }
EOF
cat >"$WORK/b.c6" <<'EOF'
int add(int a, int b);
extern int g;
int main(void) { return add(1, 2); }
EOF
if "$C6C" -c "$WORK/a.c6" -o "$WORK/a.o" >"$WORK/a.log" 2>&1 \
   && "$C6C" -c "$WORK/b.c6" -o "$WORK/b.o" >"$WORK/b.log" 2>&1; then
    if "$C6C" -o "$WORK/prog" "$WORK/b.o" "$WORK/a.o" >"$WORK/l1.log" 2>&1; then
        "$WORK/prog"
        rc=$?
        if [ "$rc" = 45 ]; then
            ok "两个 .o 链出的程序跑对（退出码 45 = 1+2+42）"
        else
            bad "两个 .o 链出的程序跑对" "退出码 $rc（应为 45）"
        fi
    else
        bad "两个 .o 链接" "$(cat "$WORK/l1.log")"
    fi
    # 链接顺序无关：符号按名字解析，不该看 .o 的先后
    if "$C6C" -o "$WORK/prog2" "$WORK/a.o" "$WORK/b.o" >"$WORK/l2.log" 2>&1; then
        "$WORK/prog2"
        rc=$?
        if [ "$rc" = 45 ]; then
            ok "链接顺序无关（a.o b.o 与 b.o a.o 都成立）"
        else
            bad "链接顺序无关" "退出码 $rc（应为 45）"
        fi
    else
        bad "链接顺序无关" "$(cat "$WORK/l2.log")"
    fi
else
    bad "纯自足的两个 .o" "$(cat "$WORK/a.log" "$WORK/b.log" 2>/dev/null)"
fi

# --- 2. 真实规模：rt.o（564 条重定位）+ 用 printf 的程序 ---
cat >"$WORK/hello.c6" <<'EOF'
int printf(const char *fmt, ...);

int main(void)
{
    printf("hello from c6c linker\n");
    return 3;
}
EOF
rt_ok=0
if "$C6C" -I "$HERE/include" -c "$RT" -o "$WORK/rt.o" >"$WORK/rt.log" 2>&1; then
    rt_ok=1
    ok "自带运行时编成 .o（$(grep -o '重定位 [0-9]* 条' "$WORK/rt.log" | head -1)）"
else
    bad "自带运行时编成 .o" "$(tail -3 "$WORK/rt.log")"
fi
if [ "$rt_ok" = 1 ] \
   && "$C6C" -c "$WORK/hello.c6" -o "$WORK/hello.o" >"$WORK/hello.log" 2>&1; then
    "$C6C" -Wl,--allow-undefined -o "$WORK/hello" "$WORK/hello.o" "$WORK/rt.o" \
        >"$WORK/hl.log" 2>"$WORK/hl.err"
    lrc=$?
    if [ "$lrc" = 0 ]; then
        got=$("$WORK/hello")
        erc=$?
        if [ "$got" = "hello from c6c linker" ] && [ "$erc" = 3 ]; then
            ok "rt.o + hello.o：链接、运行、输出与退出码都对"
        else
            bad "rt.o + hello.o 的运行结果" "输出『$got』退出码 $erc（应为 3）"
        fi
        if grep -q '未定义符号按 0 处理' "$WORK/hl.err"; then
            ok "未定义符号按 0 处理时打汇总警告"
        else
            bad "未定义符号按 0 处理时打汇总警告" "$(cat "$WORK/hl.err")"
        fi
    else
        bad "rt.o + hello.o 链接" "$(tail -3 "$WORK/hl.log")$(tail -3 "$WORK/hl.err")"
    fi
    # 不给 --allow-undefined 时必须硬报错（退出码 2 + 名字在 stderr）
    "$C6C" -o "$WORK/hello2" "$WORK/hello.o" "$WORK/rt.o" >"$WORK/h2.out" 2>"$WORK/h2.err"
    rc=$?
    if [ "$rc" = 2 ] && grep -q "未定义的符号" "$WORK/h2.err"; then
        ok "缺符号时硬报错（退出码 2，名字进 stderr）"
    else
        bad "缺符号时硬报错" "退出码 $rc：$(cat "$WORK/h2.err")"
    fi
else
    bad "hello.o" "$(tail -3 "$WORK/hello.log" 2>/dev/null)"
fi

# --- 3. 构造函数：.init_array 必须在 main 之前跑完 ---
cat >"$WORK/ctor.c6" <<'EOF'
int printf(const char *fmt, ...);

int baked = 0;

__attribute__((constructor)) static void bake(void) { baked = 99; }

int main(void)
{
    printf("baked=%d\n", baked);
    return baked == 99 ? 0 : 1;
}
EOF
if [ "$rt_ok" = 1 ] && "$C6C" -c "$WORK/ctor.c6" -o "$WORK/ctor.o" >"$WORK/ctor.log" 2>&1 \
   && "$C6C" -Wl,--allow-undefined -o "$WORK/ctorprog" "$WORK/ctor.o" "$WORK/rt.o" \
        >"$WORK/cl.log" 2>&1; then
    got=$("$WORK/ctorprog")
    erc=$?
    if [ "$got" = "baked=99" ] && [ "$erc" = 0 ]; then
        ok "构造函数在 main 之前跑完（.init_array）"
    else
        bad "构造函数在 main 之前跑完" "输出『$got』退出码 $erc"
    fi
else
    bad "构造函数用例" "$(tail -3 "$WORK/ctor.log" 2>/dev/null)$(tail -2 "$WORK/cl.log" 2>/dev/null)"
fi

# --- 4. TLS：一个 TU 带 TLS，另一个（运行时）的空记账格要被填上 ---
cat >"$WORK/tls.c6" <<'EOF'
int printf(const char *fmt, ...);

__thread int tv = 5;

int main(void)
{
    tv = tv + 2;
    printf("tv=%d\n", tv);
    return tv == 7 ? 0 : 1;
}
EOF
if [ "$rt_ok" = 1 ] && "$C6C" -c "$WORK/tls.c6" -o "$WORK/tls.o" >"$WORK/tls.log" 2>&1 \
   && "$C6C" -Wl,--allow-undefined -o "$WORK/tlsprog" "$WORK/tls.o" "$WORK/rt.o" \
        >"$WORK/tl.log" 2>&1; then
    got=$("$WORK/tlsprog")
    erc=$?
    if [ "$got" = "tv=7" ] && [ "$erc" = 0 ]; then
        ok "TLS 记账格跨 .o 传递（%fs 访问读到正确的初值）"
    else
        bad "TLS 记账格跨 .o 传递" "输出『$got』退出码 $erc"
    fi
else
    bad "TLS 用例" "$(tail -2 "$WORK/tls.log" 2>/dev/null)$(tail -2 "$WORK/tl.log" 2>/dev/null)"
fi

# --- 5. 两个 .o 都带 TLS：必须拒绝，而不是链出一个会段错的程序 ---
cat >"$WORK/tls2.c6" <<'EOF'
int printf(const char *fmt, ...);

__thread int tv2 = 11;

int hello_from_tls2(void)
{
    printf("tv2=%d\n", tv2);
    return tv2;
}
EOF
if "$C6C" -c "$WORK/tls2.c6" -o "$WORK/tls2.o" >"$WORK/tls2.log" 2>&1 \
   && "$C6C" -c "$WORK/tls.c6" -o "$WORK/tlsA.o" >"$WORK/tlsA.log" 2>&1; then
    "$C6C" -Wl,--allow-undefined -o "$WORK/two" "$WORK/tlsA.o" "$WORK/tls2.o" \
        >"$WORK/two.out" 2>"$WORK/two.err"
    rc=$?
    if [ "$rc" = 2 ] && grep -q "都带 TLS" "$WORK/two.err"; then
        ok "两个 .o 都带 TLS 时如实拒绝"
    else
        bad "两个 .o 都带 TLS 时如实拒绝" "退出码 $rc：$(cat "$WORK/two.err")"
    fi
fi

# --- 6. 产物结构：ET_EXEC + 两个 PT_LOAD + 不可执行栈 ---
if have readelf && [ -x "$WORK/prog" ]; then
    if readelf -h "$WORK/prog" | grep -q 'EXEC (可执行文件)\|EXEC (Executable file)'; then
        ok "产物是 ET_EXEC"
    else
        bad "产物是 ET_EXEC" "$(readelf -h "$WORK/prog" | grep '类型')"
    fi
    ph=$(readelf -l "$WORK/prog")
    if [ "$(echo "$ph" | grep -c '^  LOAD')" = 2 ]; then
        ok "两个 PT_LOAD（代码 R+E / 数据 R+W 分开）"
    else
        bad "两个 PT_LOAD" "$(echo "$ph" | grep '^  LOAD')"
    fi
    if echo "$ph" | grep -q 'GNU_STACK'; then
        stack_flags=$(echo "$ph" | grep -A1 'GNU_STACK' | tail -1 | awk '{print $3}')
        if [ "$stack_flags" = "RW" ]; then
            ok "栈不可执行（PT_GNU_STACK 是 RW）"
        else
            bad "栈不可执行" "标志是 $stack_flags"
        fi
    else
        bad "栈不可执行" "没有 PT_GNU_STACK"
    fi
else
    echo "skip 产物结构检查（没有 readelf 或没链出 prog）"
fi

# --- 7. 混着给 .o 和源文件要拒绝 ---
"$C6C" -o "$WORK/mix" "$WORK/a.o" "$WORK/b.c6" >"$WORK/mix.out" 2>"$WORK/mix.err"
rc=$?
if [ "$rc" = 2 ]; then
    ok "混着给 .o 和源文件以退出码 2 拒绝"
else
    bad "混着给 .o 和源文件以退出码 2 拒绝" "退出码 $rc：$(cat "$WORK/mix.err")"
fi

echo "c6c 链接器回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
