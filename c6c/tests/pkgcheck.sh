#!/bin/sh
# 库形态与安装契约的回归：没有 tcc 也照样能装、能 find_package、能 pkgconf。
#
# 验四件事：
#   1. 安装树长什么样（bin/c6 + 嵌入库 + 头文件 + 自带头文件/运行时 + 元数据）；
#   2. find_package(c6 COMPONENTS libtcc) 拿到的 c6::libtcc 真能编能跑
#      （tcc 不在场时由 libc6c + tcc_* 兼容层顶上）；
#   3. pkgconf --cflags --libs c6lib 的旗子对**宿主 C 程序**是干净的
#      （c6 自己的 stdio.h 不能盖掉系统的）；
#   4. 装到别的前缀的 c6 二进制还能自己找到自带头文件和 rt.c6。
set -u

CC=${CC:-cc}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }

if ! command -v cmake >/dev/null 2>&1; then
    echo "c6c 安装契约：跳过（没有 cmake）"
    exit 0
fi
if [ ! -x "$ROOT/build/c6c" ]; then
    bad "c6c 已构建" "先 make build"
    echo "c6c 安装契约：通过 $pass / 失败 $fail"
    exit 1
fi

PREFIX="$WORK/prefix"
if ! cmake -S "$ROOT/.." -B "$WORK/build" -DC6_ENABLE_TCC=OFF \
        -DCMAKE_BUILD_TYPE=Release >"$WORK/cfg.log" 2>&1; then
    bad "没有 tcc 也能 configure" "$(tail -3 "$WORK/cfg.log")"
    echo "c6c 安装契约：通过 $pass / 失败 $fail"
    exit 1
fi
ok "没有 tcc 也能 configure（C6_ENABLE_TCC=OFF）"

if ! cmake --install "$WORK/build" --prefix "$PREFIX" >"$WORK/inst.log" 2>&1; then
    bad "安装" "$(tail -3 "$WORK/inst.log")"
    echo "c6c 安装契约：通过 $pass / 失败 $fail"
    exit 1
fi
ok "安装完成（--prefix 到临时目录）"

# --- 1. 安装树 ---
miss=""
for f in bin/c6 include/c6lib.h include/libtcc.h lib/libc6c.a lib/libc6c.so \
         lib/c6/rt.c6 lib/c6/include/stddef.h lib/c6/include/stdarg.h \
         lib/pkgconfig/c6.pc lib/pkgconfig/c6lib.pc lib/cmake/c6/c6Config.cmake; do
    [ -e "$PREFIX/$f" ] || miss="$miss $f"
done
if [ -z "$miss" ]; then
    ok "安装树齐全（bin/c6 / 嵌入库 / 兼容头 / lib/c6 自带头与运行时 / pc / cmake）"
else
    bad "安装树齐全" "缺：$miss"
fi

if [ -e "$PREFIX/include/stdio.h" ]; then
    bad "include/ 不装 c6 自己的 stdio.h" "会把宿主程序的系统头文件盖掉"
else
    ok "include/ 只放宿主用的头文件（c6 自己的在 lib/c6/include）"
fi

# --- 2. find_package(c6 COMPONENTS libtcc) ---
mkdir -p "$WORK/consumer"
cat >"$WORK/consumer/main.c" <<'EOF'
#include <stdio.h>
#include <libtcc.h>

int main(int argc, char **argv)
{
    TCCState *s = tcc_new();
    int rc;

    if (!s)
        return 1;
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    if (tcc_compile_string(s, "int printf(const char*,...);\n"
                              "int main(void){printf(\"pkg ok\\n\");return 9;}\n") != 0)
        return 2;
    tcc_relocate(s, TCC_RELOCATE_AUTO);
    rc = tcc_run(s, argc, argv);
    tcc_delete(s);
    return rc == 9 ? 0 : 3;
}
EOF
cat >"$WORK/consumer/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(consumer C)
find_package(c6 0.1 REQUIRED COMPONENTS libtcc)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE c6::libtcc)
EOF
if cmake -S "$WORK/consumer" -B "$WORK/consumer/b" \
        -DCMAKE_PREFIX_PATH="$PREFIX" >"$WORK/cons-cfg.log" 2>&1 &&
        cmake --build "$WORK/consumer/b" >"$WORK/cons-b.log" 2>&1; then
    got=$("$WORK/consumer/b/consumer")
    if [ "$got" = "pkg ok" ]; then
        ok "find_package(c6 COMPONENTS libtcc)：c6::libtcc 真能编能跑"
    else
        bad "find_package 消费者运行" "输出『$got』"
    fi
else
    bad "find_package(c6 COMPONENTS libtcc)" "$(tail -3 "$WORK/cons-cfg.log" "$WORK/cons-b.log" 2>/dev/null | tr '\n' ' ')"
fi

# --- 3. pkgconf 旗子对宿主 C 程序是干净的 ---
if command -v pkgconf >/dev/null 2>&1 || command -v pkg-config >/dev/null 2>&1; then
    PC=$(command -v pkgconf || command -v pkg-config)
    flags=$(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" "$PC" --cflags --libs c6lib 2>"$WORK/pc.log")
    if [ -z "$flags" ]; then
        bad "pkgconf c6lib" "$(cat "$WORK/pc.log")"
    elif ! "$CC" -O1 -Wall -Werror -o "$WORK/pc-cons" "$WORK/consumer/main.c" $flags \
            >"$WORK/pc-cc.log" 2>&1; then
        bad "pkgconf 旗子能编宿主程序（-Wall -Werror）" "$(tail -3 "$WORK/pc-cc.log")"
    else
        got=$(LD_LIBRARY_PATH="$PREFIX/lib" "$WORK/pc-cons")
        if [ "$got" = "pkg ok" ]; then
            ok "pkgconf --cflags --libs c6lib：-Wall -Werror 编得过、跑得通"
        else
            bad "pkgconf 消费者运行" "输出『$got』"
        fi
    fi
else
    echo "skip pkgconf（没装 pkgconf/pkg-config）"
fi

# --- 4. 装到别处的前缀，二进制自己找自带头文件与运行时 ---
cat >"$WORK/self.c6" <<'EOF'
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
int printf(const char *fmt, ...);
int main(void) { printf("self ok %zu\n", (size_t)3); return 0; }
EOF
if "$PREFIX/bin/c6" -o "$WORK/self" "$WORK/self.c6" >"$WORK/self.log" 2>&1; then
    got=$("$WORK/self")
    if [ "$got" = "self ok 3" ]; then
        ok "装到别的前缀的 c6 自找 include/ 与 rt.c6"
    else
        bad "装出来的 c6 编出的程序" "输出『$got』"
    fi
else
    bad "装出来的 c6 编译" "$(tail -3 "$WORK/self.log")"
fi

echo "c6c 安装契约：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
