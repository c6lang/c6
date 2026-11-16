#!/bin/sh
# ctu 后端验收：同一份源走两条路，运行输出必须逐字相同。
#
#   1) c6c --emit-ctu -c -o <dir>/  →  a.c / b.c  →  gcc  →  链接 cside.c
#   2) gcc 直接编 a.c6 / b.c6（它们本来就只用了普通 C）      →  链接 cside.c
#
# 第一条路量的是"c6c 的 per-TU C 后端"：一个 TU 一个 .c、真 C ABI、不要 main、
# 不带 c6c 自己的运行时；第二条路是参照物（同一个语义由 gcc 自己表达）。
#
# 用法：C6C=<path to c6c> sh c6c/tests/ctu/run.sh
# 退出码 0 = 通过。

set -e

DIR=$(cd "$(dirname "$0")" && pwd)
CC=${CC:-gcc}
C6C=${C6C:-"$DIR/../../build/c6c"}
C6C=$(cd "$(dirname "$C6C")" && pwd)/$(basename "$C6C")
T=${TMPDIR:-/tmp}/c6c-ctu-test

rm -rf "$T"
mkdir -p "$T/ctu" "$T/ref"

# --- 1) c6c 的 ctu 后端：一个 TU 一个 .c（-o 给目录） ---
"$C6C" --emit-ctu -c -o "$T/ctu/" "$DIR/a.c6" "$DIR/b.c6"
test -f "$T/ctu/a.c" || { echo "ctu: 没产出 a.c"; exit 1; }
test -f "$T/ctu/b.c" || { echo "ctu: 没产出 b.c"; exit 1; }

# --- 2) 参照物：同一份源直接当 C 编 ---
cp "$DIR/a.c6" "$T/ref/a.c"
cp "$DIR/b.c6" "$T/ref/b.c"

for d in ctu ref; do
    for f in a b; do
        $CC -c -o "$T/$d/$f.o" "$T/$d/$f.c"
    done
    $CC -c -o "$T/$d/cside.o" "$DIR/cside.c"
    $CC -o "$T/$d.t" "$T/$d/a.o" "$T/$d/b.o" "$T/$d/cside.o"
done

"$T/ctu.t" > "$T/ctu.out"
"$T/ref.t" > "$T/ref.out"

if ! cmp -s "$T/ctu.out" "$T/ref.out"; then
    echo "ctu: 输出和参照物不一致"
    diff -u "$T/ref.out" "$T/ctu.out" || true
    exit 1
fi

echo "ctu: 通过（$(cat "$T/ctu.out" | tr '\n' '|')）"
