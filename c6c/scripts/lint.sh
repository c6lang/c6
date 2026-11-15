#!/bin/sh
# c6c 隔离检查（plan.md ADR-1 / design.md §11）：
#   1. 不 include tcc/ 的头
#   2. 不链接 libtcc、不引用 tcc 的 .c
#   3. 源码里不出现 LGPL 头（tcc/ 是 LGPL-2.1，c6c 是 MIT，边界必须机器可查）
#   4. 必需文件都在
#
# 扫描范围：c6c 自己的 C 源码 + c6c/Makefile 的非注释行。
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

if [ -f Makefile ]; then
    # 去掉整行注释和行尾注释再查：BDIR 那行的注释里提到 libtcc1.a（tcc 的运行时
    # 静态库，不是 libtcc），不剥掉行尾注释会误报。
    hits=$(sed -E 's/#.*$//' Makefile | grep -nE 'libtcc([^1]|$)|-ltcc' || true)
    [ -z "$hits" ] || die "c6c/Makefile 不得链接 libtcc：
$hits"
fi

for f in README.md LICENSE docs/plan.md docs/design.md docs/todo.md \
         bench/bench.py bench/baseline.json bench/thresholds.json; do
    [ -e "$f" ] || die "缺文件：$f"
done

[ "$fail" -eq 0 ] && echo "lint: 通过（目录边界、许可证、必需文件）"
exit "$fail"
