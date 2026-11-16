#!/bin/sh
# c6c 隔离检查（plan.md ADR-1 / design.md §11）：
#   1. 不 include tcc/ 的头
#   2. 不链接 libtcc、不引用 tcc 的 .c
#   3. 源码里不出现 LGPL 头（tcc/ 是 LGPL-2.1，c6c 是 MIT，边界必须机器可查）
#   4. 必需文件都在
#
# 扫描范围：c6c 自己的 C 源码 + 定义 c6c 构建的那两份 CMake 文件
# （cmake/C6C.cmake、根 CMakeLists.txt）——c6c 已经没有自己的 Makefile 了。
# 文档（*.md）与 bench/ 允许提到 tcc：前者讲边界，后者的语料就是 tcc.c。
#
# 两个坑（都踩过）：
#   - 不能用 `if hits=$(grep ...)` 判“有没有命中”：xargs 无输入时整条管道返回 0，
#     会假阳性；只看输出是否为空。
#   - 源码目录当前可能是空的（P0 阶段），xargs -0 -r 保证不起 grep。
set -e
cd "$(dirname "$0")/.."

fail=0
die() { printf 'lint: %s\n' "$1" >&2; fail=1; }

src_c() { find . -type f \( -name '*.c' -o -name '*.h' -o -name '*.c6' \) -print0; }

scan() { # scan <label> <grep args...>
    label=$1
    shift
    hits=$(src_c | xargs -0 -r grep "$@" 2>/dev/null || true)
    [ -z "$hits" ] || die "$label
$hits"
}

scan "c6c 不得 include tcc/ 的头：" -nE '#include[[:space:]]*[<"][^">]*tcc/'
# libtcc1.a 是 tcc 的运行时库（要链接，-B 找它），libtcc 本体才是禁的
scan "c6c 不得链接 libtcc：" -nE 'libtcc([^1]|$)|-ltcc'
scan "c6c 不得引用 tcc 的 .c：" -nE '(^|[^a-zA-Z0-9_/])tcc/[a-z0-9_]+\.c'
scan "c6c 源码里不得出现 LGPL 头：" -lniE 'gnu (lesser|library) general public license|LGPL'

# 构建定义在 CMake 里：去掉 `#` 注释再查（注释里讲边界是允许的）。
# 这里只查"链接"形态（-ltcc / libtcc.so / libtcc.a）：`RENAME "libtcc.h"` 是
# 把 tcc 兼容头装成 tcc 的标准头名，跟链接别家库不是一回事。
for f in ../cmake/C6C.cmake ../CMakeLists.txt; do
    [ -f "$f" ] || continue
    hits=$(sed -E 's/#.*$//' "$f" | grep -nE -- '-ltcc|libtcc\.(so|a)($|[^0-9a-zA-Z_])' || true)
    [ -z "$hits" ] || die "$f 不得链接 libtcc：
$hits"
done

for f in README.md LICENSE docs/plan.md docs/design.md docs/todo.md \
         bench/bench.py bench/baseline.json bench/thresholds.json; do
    [ -e "$f" ] || die "缺文件：$f"
done

[ "$fail" -eq 0 ] && echo "lint: 通过（目录边界、许可证、必需文件）"
exit "$fail"
