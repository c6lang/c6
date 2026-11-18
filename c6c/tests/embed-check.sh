#!/bin/sh
# 嵌入 API（include/c6lib.h + src/embed.c6）的回归：把 c6c 当库用。
#
# 验的是"把编译器嵌进宿主程序"那种用法到底成不成立（只有 c6_* API）：
#   * 宿主程序链上 libc6c，把一段 c6 源码编进内存、就地跑 main，拿到返回值；
#   * c6_get_symbol 能取到地址；printf 这类要靠自带运行时，必须真的打出来；
#   * 编译失败要能被宿主发现（c6_error 有话说，不静默）；
#   * c6_output_file 落盘的程序能自己跑（相当于库形态的 -o）；
#   * 静态库（.a）和动态库（.so）两种链接方式都要能跑通。
set -u

LIB=${C6C_LIB:-./build}
INC=${C6C_INC:-./include}
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

# 头文件单独放一处：c6c/include 里有它自己的 stdio.h，直接 -I 会盖住系统的
mkdir -p "$WORK/inc"
cp "$INC/c6lib.h" "$WORK/inc/"

cat >"$WORK/drv.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <c6lib.h>

static const char *SRC =
    "int printf(const char *fmt, ...);\n"
    "int main(void) { printf(\"embedded hello\\n\"); return 42; }\n";

int main(int argc, char **argv)
{
    C6State *s = c6_new();
    int rc = 0;

    if (!s)
        return 100;
    if (c6_compile_string(s, SRC) != 0) {
        fprintf(stderr, "compile: %s\n", c6_error(s));
        return 101;
    }
    if (!c6_get_symbol(s, "main")) {
        fprintf(stderr, "get_symbol: 取不到 main\n");
        return 102;
    }
    rc = c6_run(s, argc, argv);
    if (rc != 42) {
        fprintf(stderr, "run: main 返回 %d（应为 42）\n", rc);
        return 103;
    }
    if (c6_output_file(s, "out.bin") != 0) {
        fprintf(stderr, "output_file: %s\n", c6_error(s));
        return 104;
    }
    c6_delete(s);
    return 0;
}
EOF

# --- 1. 动态库 ---
if [ ! -f "$LIB/libc6c.so" ]; then
    bad "libc6c.so 存在" "先 make libc6c"
else
    if "$CC" -O1 -I "$WORK/inc" -o "$WORK/drv.so" "$WORK/drv.c" \
            -L "$LIB" -lc6c -Wl,-rpath,"$LIB" >"$WORK/cc1.log" 2>&1; then
        got=$(cd "$WORK" && ./drv.so)
        rc=$?
        if [ "$rc" = 0 ] && [ "$got" = "embedded hello" ]; then
            ok "动态库：编进内存、就地跑 main、printf 走自带运行时"
        else
            bad "动态库：编进内存并跑起来" "退出码 $rc，输出『$got』"
        fi
    else
        bad "动态库链接宿主程序" "$(tail -3 "$WORK/cc1.log")"
    fi
fi

# --- 2. 静态库 ---
if [ ! -f "$LIB/libc6c.a" ]; then
    bad "libc6c.a 存在" "先 make libc6c"
else
    if "$CC" -O1 -I "$WORK/inc" -o "$WORK/drv.a" "$WORK/drv.c" \
            "$LIB/libc6c.a" >"$WORK/cc2.log" 2>&1; then
        got=$(cd "$WORK" && ./drv.a)
        rc=$?
        if [ "$rc" = 0 ] && [ "$got" = "embedded hello" ]; then
            ok "静态库：同一个宿主程序也能跑通"
        else
            bad "静态库：编进内存并跑起来" "退出码 $rc，输出『$got』"
        fi
    else
        bad "静态库链接宿主程序" "$(tail -3 "$WORK/cc2.log")"
    fi
fi

# --- 3. c6_output_file 落盘的产物能自己跑 ---
if [ -f "$WORK/out.bin" ]; then
    got=$(cd "$WORK" && ./out.bin)
    rc=$?
    if [ "$got" = "embedded hello" ] && [ "$rc" = 42 ]; then
        ok "c6_output_file 落盘的程序独立跑通（输出与退出码都对）"
    else
        bad "c6_output_file 落盘的程序" "输出『$got』退出码 $rc（应为 42）"
    fi
else
    bad "c6_output_file 落盘" "没生成 out.bin"
fi

# --- 4. 编译失败要能被宿主发现（不是静默成功） ---
cat >"$WORK/bad.c" <<'EOF'
#include <stdio.h>
#include <c6lib.h>

int main(int argc, char **argv)
{
    C6State *s = c6_new();

    (void)argc;
    (void)argv;
    if (c6_compile_string(s, "int main(void) { return }\n") == 0) {
        printf("BAD：该失败却成功了\n");
        return 1;
    }
    if (!c6_error(s)[0]) {
        printf("BAD：失败了但 c6_error 是空的\n");
        return 1;
    }
    printf("ok: %s\n", c6_error(s));
    c6_delete(s);
    return 0;
}
EOF
if "$CC" -O1 -I "$WORK/inc" -o "$WORK/bad" "$WORK/bad.c" "$LIB/libc6c.a" \
        >"$WORK/cc3.log" 2>&1; then
    out=$(cd "$WORK" && ./bad 2>/dev/null)
    rc=$?
    if [ "$rc" = 0 ]; then
        ok "编译失败被如实报告（c6_error 有话说）"
    else
        bad "编译失败被如实报告" "退出码 $rc：$out"
    fi
else
    bad "坏程序用例的编译" "$(tail -3 "$WORK/cc3.log")"
fi

# --- 5. 一个 state 只能编一次（不做静默的错结果） ---
cat >"$WORK/twice.c" <<'EOF'
#include <stdio.h>
#include <c6lib.h>

int main(void)
{
    C6State *s = c6_new();

    if (c6_compile_string(s, "int main(void) { return 1; }\n") != 0) {
        printf("BAD：第一次就失败了\n");
        return 1;
    }
    if (c6_compile_string(s, "int main(void) { return 2; }\n") == 0) {
        printf("BAD：第二次编译没有拒绝\n");
        return 1;
    }
    printf("ok: %s\n", c6_error(s));
    c6_delete(s);
    return 0;
}
EOF
if "$CC" -O1 -I "$WORK/inc" -o "$WORK/twice" "$WORK/twice.c" "$LIB/libc6c.a" \
        >"$WORK/cc4.log" 2>&1; then
    out=$(cd "$WORK" && ./twice 2>/dev/null)
    rc=$?
    if [ "$rc" = 0 ]; then
        ok "同一个 state 第二次编译被拒绝（不做静默错结果）"
    else
        bad "同一个 state 第二次编译被拒绝" "退出码 $rc：$out"
    fi
else
    bad "两次编译用例的编译" "$(tail -3 "$WORK/cc4.log")"
fi

echo "c6c 嵌入 API 回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
