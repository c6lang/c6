#!/bin/sh
# 库形态与安装契约的回归：没有 tcc 也照样能装、能 find_package、能 pkgconf。
#
# 验六件事：
#   1. 安装树长什么样（bin/c6 + 嵌入库 + c6lib.h + 自带头文件/运行时 + 元数据）；
#   2. 安装树里没有 libtcc.h，find_package(c6 COMPONENTS libtcc) 也不再被
#      libc6c 顶上（要 libtcc 请用 -DC6_ENABLE_TCC=ON 的 c6 包）；
#   3. find_package(c6c COMPONENTS compiler libc6c) 拿到 c6c 自己的名字
#      （c6 与 c6c 是两个产品，各有各的包名与 pkgconf 名）；
#   4. pkgconf --cflags --libs c6lib / c6c 的旗子对**宿主 C 程序**是干净的
#      （c6 自己的 stdio.h 不能盖掉系统的）；
#   5. 装到别的前缀的 c6 二进制还能自己找到自带头文件和 rt.c6。
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
# libdir 显式钉成 lib：下面断言的就是这套布局。不钉的话它跟着环境走——CMake 把
# `CMAKE_INSTALL_PREFIX` 环境变量当默认前缀，Debian 系的 GNUInstallDirs 一看
# prefix=/usr 就把 libdir 定成 lib/x86_64-linux-gnu，安装树里的 lib/libc6c.a、
# lib/pkgconfig/c6lib.pc 全换了地方，断言和 pkgconf 就一起假红（CI 上见过）。
if ! cmake -S "$ROOT/.." -B "$WORK/build" -DC6_ENABLE_TCC=OFF \
        -DCMAKE_INSTALL_LIBDIR=lib \
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
for f in bin/c6 bin/c6c include/c6lib.h lib/libc6c.a lib/libc6c.so \
         lib/c6/rt.c6 lib/c6/include/stddef.h lib/c6/include/stdarg.h \
         lib/pkgconfig/c6.pc lib/pkgconfig/c6lib.pc lib/pkgconfig/c6c.pc \
         lib/cmake/c6/c6Config.cmake lib/cmake/c6c/c6cConfig.cmake; do
    [ -e "$PREFIX/$f" ] || miss="$miss $f"
done
if [ -z "$miss" ]; then
    ok "安装树齐全（bin/c6 + bin/c6c / 嵌入库 / 宿主头 / lib/c6 自带头与运行时 / pc / cmake）"
else
    bad "安装树齐全" "缺：$miss"
fi

if [ -e "$PREFIX/include/libtcc.h" ]; then
    bad "安装树里没有 libtcc.h" "c6c 不提供 tcc 兼容头"
else
    ok "安装树里没有 libtcc.h（宿主头只有 c6lib.h）"
fi

if [ -e "$PREFIX/include/stdio.h" ]; then
    bad "include/ 不装 c6 自己的 stdio.h" "会把宿主程序的系统头文件盖掉"
else
    ok "include/ 只放宿主用的头文件（c6 自己的在 lib/c6/include）"
fi

# --- 2. 无 tcc 的安装里没有 c6::libtcc，也不该拿 libc6c 冒充 ---
mkdir -p "$WORK/consumer"
cat >"$WORK/consumer/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(consumer C)
find_package(c6 0.1 REQUIRED COMPONENTS libtcc)
EOF
if cmake -S "$WORK/consumer" -B "$WORK/consumer/b" \
        -DCMAKE_PREFIX_PATH="$PREFIX" >"$WORK/cons-cfg.log" 2>&1; then
    bad "find_package(c6 COMPONENTS libtcc) 在无 tcc 包里本该失败"
else
    ok "find_package(c6 COMPONENTS libtcc) 在无 tcc 包里按预期失败"
fi

# --- 3. find_package(c6c COMPONENTS compiler libc6c) ---
# c6c 有自己的包名：它不是"另一个 c6"，下游依赖的是自举编译器 + libc6c。
mkdir -p "$WORK/consumer-c6c"
cat >"$WORK/consumer/main.c" <<'EOF'
#include <stdio.h>
#include <c6lib.h>

int main(int argc, char **argv)
{
    C6State *s = c6_new();
    int rc;

    if (!s)
        return 1;
    if (c6_compile_string(s, "int printf(const char*,...);\n"
                              "int main(void){printf(\"pkg ok\\n\");return 9;}\n") != 0)
        return 2;
    rc = c6_run(s, argc, argv);
    c6_delete(s);
    return rc == 9 ? 0 : 3;
}
EOF
cp "$WORK/consumer/main.c" "$WORK/consumer-c6c/main.c"
cat >"$WORK/consumer-c6c/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(consumer-c6c C)
find_package(c6c 0.1 REQUIRED COMPONENTS compiler libc6c)
if(NOT TARGET c6c::compiler)
  message(FATAL_ERROR "缺 c6c::compiler（bin/c6c 没被找到）")
endif()
if(NOT TARGET c6c::libc6c)
  message(FATAL_ERROR "缺 c6c::libc6c（libc6c.a 没被找到）")
endif()
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE c6c::libc6c)
EOF
if cmake -S "$WORK/consumer-c6c" -B "$WORK/consumer-c6c/b" \
        -DCMAKE_PREFIX_PATH="$PREFIX" >"$WORK/cons6-cfg.log" 2>&1 &&
        cmake --build "$WORK/consumer-c6c/b" >"$WORK/cons6-b.log" 2>&1; then
    got=$("$WORK/consumer-c6c/b/consumer")
    if [ "$got" = "pkg ok" ]; then
        ok "find_package(c6c COMPONENTS compiler libc6c)：c6c::libc6c 真能编能跑"
    else
        bad "c6c find_package 消费者运行" "输出『$got』"
    fi
else
    bad "find_package(c6c COMPONENTS compiler libc6c)" \
        "$(tail -3 "$WORK/cons6-cfg.log" "$WORK/cons6-b.log" 2>/dev/null | tr '\n' ' ')"
fi

# --- 4. pkgconf 旗子对宿主 C 程序是干净的 ---
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
    # c6c 的 pkgconf 名：同一个库、无歧义的名字
    flags6=$(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" "$PC" --cflags --libs c6c 2>"$WORK/pc6.log")
    if [ -z "$flags6" ]; then
        bad "pkgconf c6c" "$(cat "$WORK/pc6.log")"
    elif ! "$CC" -O1 -Wall -Werror -o "$WORK/pc-cons6" "$WORK/consumer/main.c" $flags6 \
            >"$WORK/pc6-cc.log" 2>&1; then
        bad "pkgconf c6c 旗子能编宿主程序（-Wall -Werror）" "$(tail -3 "$WORK/pc6-cc.log")"
    else
        got=$(LD_LIBRARY_PATH="$PREFIX/lib" "$WORK/pc-cons6")
        if [ "$got" = "pkg ok" ]; then
            ok "pkgconf --cflags --libs c6c：-Wall -Werror 编得过、跑得通"
        else
            bad "pkgconf c6c 消费者运行" "输出『$got』"
        fi
    fi
else
    echo "skip pkgconf（没装 pkgconf/pkg-config）"
fi

# --- 5. 装到别处的前缀，二进制自己找自带头文件与运行时 ---
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

# bin/c6c 是同一个自举编译器的第二个名字（默认配置里它与 tcc 版 c6 并存）：
# 它按 argv[0] 推目录的规则同样要成立。
if "$PREFIX/bin/c6c" -o "$WORK/self6c" "$WORK/self.c6" >"$WORK/self6c.log" 2>&1; then
    got=$("$WORK/self6c")
    if [ "$got" = "self ok 3" ]; then
        ok "bin/c6c 也自找 include/ 与 rt.c6"
    else
        bad "bin/c6c 编出的程序" "输出『$got』"
    fi
else
    bad "bin/c6c 编译" "$(tail -3 "$WORK/self6c.log")"
fi

echo "c6c 安装契约：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
