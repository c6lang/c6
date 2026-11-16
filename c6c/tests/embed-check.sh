#!/bin/sh
# 嵌入 API（include/c6lib.h + src/embed.c6）的回归：把 c6c 当库用。
#
# 验的是"libtcc 那种用法"到底成不成立：
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

# --- 6. tcc_* 兼容层：原本写 tcc 嵌入 API 的程序不改一行就能跑 ---
cp "$INC/tcc-compat.h" "$WORK/inc/"
cat >"$WORK/shim.c" <<'INNER'
#include <stdio.h>
#include "tcc-compat.h"

int main(int argc, char **argv)
{
    TCCState *s = tcc_new();
    int rc = 0;

    if (!s)
        return 1;
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    tcc_define_symbol(s, "SHIM_BUILT", "1");
    if (tcc_compile_string(s, "int printf(const char *, ...);\n"
                              "int main(void) { printf(\"shim ok\\n\"); return 7; }\n") != 0)
        return 2;
    if (tcc_relocate(s, TCC_RELOCATE_AUTO) != 0)
        return 3;
    rc = tcc_run(s, argc, argv);
    tcc_delete(s);
    return rc == 7 ? 0 : 4;
}
INNER
if "$CC" -O1 -I "$WORK/inc" -o "$WORK/shim" "$WORK/shim.c" "$LIB/libc6c.a" \
        >"$WORK/cc5.log" 2>&1; then
    got=$(cd "$WORK" && ./shim 2>/dev/null)
    rc=$?
    if [ "$rc" = 0 ] && [ "$got" = "shim ok" ]; then
        ok "tcc_* 兼容层：tcc_new/compile_string/relocate/run 一条龙"
    else
        bad "tcc_* 兼容层" "退出码 $rc，输出『$got』"
    fi
else
    bad "tcc_* 兼容层的编译" "$(tail -3 "$WORK/cc5.log")"
fi

# --- 7. tcc_* 兼容面：tcc/libtcc.h 那一整套名字，能做的做、做不到的如实报错 ---
mkdir -p "$WORK/inc2" "$WORK/goodrt/lib" "$WORK/badrt/lib"
printf '#define SYS_RET 13\n' >"$WORK/inc2/compat_sys.h"
cp "$INC/../lib/rt.c6" "$WORK/goodrt/lib/rt.c6"
printf '/* 故意写坏：用来证明运行时真的是从这个目录取的 */\nint bad(\n' \
    >"$WORK/badrt/lib/rt.c6"
cat >"$WORK/shim2.c" <<'INNER'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "tcc-compat.h"

static int nerr = 0;
static char firstmsg[512] = {0};

static void on_err(void *opaque, const char *msg)
{
    (void)opaque;
    if (nerr == 0)
        snprintf(firstmsg, sizeof(firstmsg), "%s", msg);
    nerr++;
}

static int nsym = 0;
static int saw_main = 0;

static void on_sym(void *ctx, const char *name, const void *val)
{
    (void)ctx;
    nsym++;
    if (strcmp(name, "main") == 0 && val != NULL)
        saw_main = 1;
}

int main(int argc, char **argv)
{
    TCCState *s = NULL;
    int saved = -1;
    int tmp = -1;
    long fsz = 0;
    FILE *f = NULL;
    int rc = 0;
    int stage = 0;

    /* 一个进程只能编一次（c6c 的全局状态还没做重置），所以这套用例按 stage
     * 分开跑：每段由 shell 起一个新进程。 */
    if (argc < 5)
        return 90;
    stage = atoi(argv[4]);
    if (stage < 1)
        return 91;
    switch (stage) {
    case 1:

    /* 1. tcc_set_error_func：诊断整行交给回调，stderr 上什么都不该剩下 */
    s = tcc_new();
    tcc_set_error_func(s, NULL, on_err);
    saved = dup(2);
    tmp = open("shim-stderr.txt", O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (saved < 0 || tmp < 0) {
        printf("BAD：重定向不了 stderr\n");
        return 1;
    }
    dup2(tmp, 2);
    rc = tcc_compile_string(s, "int main(void) { return }\n");
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    close(tmp);
    if (rc == 0) {
        printf("BAD：坏源码该失败却成功了\n");
        return 2;
    }
    if (nerr == 0 || !firstmsg[0]) {
        printf("BAD：回调没收到诊断（nerr=%d msg=『%s』）\n", nerr, firstmsg);
        return 3;
    }
    f = fopen("shim-stderr.txt", "rb");
    if (!f) {
        printf("BAD：读不到 stderr 抓包文件\n");
        return 4;
    }
    fseek(f, 0, SEEK_END);
    fsz = ftell(f);
    fclose(f);
    if (fsz != 0) {
        printf("BAD：诊断还是打到了 stderr（%ld 字节）\n", fsz);
        return 5;
    }
    printf("tcc_set_error_func：回调收到 %d 条诊断（首条『%s』），stderr 干净\n",
           nerr, firstmsg);
    tcc_delete(s);
    return 0;
    case 2:

    /* 2. tcc_add_sysinclude_path + 一参数的 tcc_relocate + tcc_list_symbols */
    s = tcc_new();
    if (tcc_add_sysinclude_path(s, argv[1]) != 0) {
        printf("BAD：tcc_add_sysinclude_path 失败：%s\n", c6_strerror(s));
        return 6;
    }
    if (tcc_compile_string(s, "#include <compat_sys.h>\n"
                              "int main(void) { return SYS_RET; }\n") != 0) {
        printf("BAD：系统头目录里的头没找到：%s\n", c6_strerror(s));
        return 7;
    }
    if (tcc_relocate(s) != 0) {
        printf("BAD：tcc_relocate(s) 一个参数的写法失败：%s\n", c6_strerror(s));
        return 8;
    }
    tcc_list_symbols(s, NULL, on_sym);
    if (nsym == 0 || !saw_main) {
        printf("BAD：tcc_list_symbols 给了 %d 个符号（main=%d）\n", nsym, saw_main);
        return 9;
    }
    rc = tcc_run(s, 1, argv);
    if (rc != 13) {
        printf("BAD：run 返回 %d（应为 13）\n", rc);
        return 10;
    }
    printf("tcc_add_sysinclude_path + 一参数 relocate + list_symbols(%d 个) + run=13\n",
           nsym);
    tcc_delete(s);
    return 0;
    case 3:

    /* 3. 做不到的三个：返回 -1 且 c6_strerror 说明原因；空实现的收下不炸 */
    s = tcc_new();
    if (tcc_add_library_path(s, "/tmp") != -1 || strstr(c6_strerror(s), "-L") == NULL) {
        printf("BAD：tcc_add_library_path 没如实拒绝：%s\n", c6_strerror(s));
        return 11;
    }
    if (tcc_add_library(s, "m") != -1 || strstr(c6_strerror(s), "-l") == NULL) {
        printf("BAD：tcc_add_library 没如实拒绝：%s\n", c6_strerror(s));
        return 12;
    }
    if (tcc_add_symbol(s, "host_fn", (const void *)&on_sym) != -1 ||
        strstr(c6_strerror(s), "JIT") == NULL) {
        printf("BAD：tcc_add_symbol 没如实拒绝：%s\n", c6_strerror(s));
        return 13;
    }
    tcc_set_realloc(NULL);
    tcc_set_backtrace_func(s, NULL, NULL);
    printf("tcc_add_library_path/add_library/add_symbol 返回 -1 并说明原因；"
           "set_realloc/set_backtrace_func 收下不炸\n");
    tcc_delete(s);
    return 0;
    case 4:

    /* 4. tcc_set_lib_path：认 <dir>/lib/rt.c6，并且真的从那儿取运行时 */
    s = tcc_new();
    tcc_set_lib_path(s, "/nonexistent-c6c-libdir");
    if (strstr(c6_strerror(s), "找不到") == NULL) {
        printf("BAD：tcc_set_lib_path 对坏目录没报错：%s\n", c6_strerror(s));
        return 14;
    }
    tcc_delete(s);

    s = tcc_new();
    tcc_set_lib_path(s, argv[2]);     /* 坏 rt.c6：编译必须失败 */
    if (tcc_compile_string(s, "int main(void) { return 1; }\n") == 0) {
        printf("BAD：坏的 rt.c6 也编过了，说明运行时不是从 set_lib_path 来的\n");
        return 15;
    }
    tcc_delete(s);

    return 0;
    case 5:

    s = tcc_new();
    tcc_set_lib_path(s, argv[3]);     /* 好 rt.c6：编 + 跑 */
    if (c6_strerror(s)[0]) {
        printf("BAD：tcc_set_lib_path 对好目录报错：%s\n", c6_strerror(s));
        return 16;
    }
    if (tcc_compile_string(s, "int main(void) { return 21; }\n") != 0) {
        printf("BAD：从 <dir>/lib/rt.c6 取运行时时编译失败：%s\n", c6_strerror(s));
        return 17;
    }
    rc = tcc_run(s, 2, argv);
    if (rc != 21) {
        printf("BAD：run 返回 %d（应为 21）\n", rc);
        return 18;
    }
    printf("tcc_set_lib_path 指向 <dir>/lib/rt.c6 真的生效（run=21）\n");
    tcc_delete(s);

    return 0;
    default:
        return 92;
    }
}
INNER
if "$CC" -O1 -I "$WORK/inc" -o "$WORK/shim2" "$WORK/shim2.c" "$LIB/libc6c.a" \
        >"$WORK/cc6.log" 2>&1; then
    stfail=0
    : >"$WORK/shim2.out"
    : >"$WORK/shim2.err"
    for st in 1 2 3 4 5; do
        (cd "$WORK" && ./shim2 "$WORK/inc2" "$WORK/badrt" "$WORK/goodrt" "$st" \
            2>>"$WORK/shim2.err") >>"$WORK/shim2.out"
        rc=$?
        if [ "$rc" != 0 ]; then
            stfail=1
            bad "tcc_* 兼容面（stage $st）" "退出码 $rc：$(tail -2 "$WORK/shim2.out")"
        fi
    done
    if [ "$stfail" = 0 ]; then
        while IFS= read -r line; do
            ok "兼容面：$line"
        done <"$WORK/shim2.out"
    fi
else
    bad "tcc_* 兼容面的编译" "$(tail -3 "$WORK/cc6.log")"
fi

echo "c6c 嵌入 API 回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
