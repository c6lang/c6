#!/bin/sh
# G2c 记分板：用 **c6c 编出来的 tcc**（build/tcc-c6c）跑上游 tests2。
#
# 和目标（3）的区别：那条是 c6c **自己**直接编 tests2；这条是验收（2）的
# 后半段——c6c 产出的 tcc 必须能把它自己的测试套件跑起来。tcc 要 `-B` 指向
# tcc 的源码目录才能找到自己的头文件和 libtcc1.a。
#
# 用法：tests/g2c-tests2.sh [-v] [用例名前缀]
set -u
TCC=${TCC:-build/tcc-c6c}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${ROOT:-$HERE/../../..}
TCCDIR=${TCCDIR:-$ROOT/tcc/tests/tests2}
VERBOSE=0
FILTER=""
for a in "$@"; do
    case "$a" in
    -v) VERBOSE=1 ;;
    *) FILTER=$a ;;
    esac
done
if [ ! -x "$TCC" ]; then
    echo "g2c: 找不到 $TCC（先 make build-tcc）" >&2
    exit 2
fi
# 下面要 cd 进用例目录再跑（让诊断里的文件名是裸的），所以先把 TCC 变成绝对路径
TCC=$(cd "$(dirname "$TCC")" && pwd)/$(basename "$TCC")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0
cerr=0
nolink=0
cstale=0
for src in "$TCCDIR"/*.c; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .c)
    case "$base" in
    *"$FILTER"*) ;;
    *) continue ;;
    esac
    # `-lm`：tcc 自己跑 tests2 时也给数学库用例加这个（22_floating_point /
    # 24_math_library 里的 sin/cos 得从 libm 解出来）。
    # 在用例目录里、用**裸文件名**调用：上游 Makefile 就是这么跑的，诊断信息
    # 里的文件名是裸的（`149_parameter_qualifier_errors.c:5: warning: …`）。
    # 传全路径的话 tcc 会把全路径打出来，和 expect 对不上——那是调用方式的
    # 差别，不是编译器的问题。
    if ! ( cd "$TCCDIR" && "$TCC" -B"$ROOT/tcc" -lm "$base.c" -o "$TMP/$base" ) \
            >"$TMP/$base.log" 2>&1; then
        if grep -q '没有 main\|no main' "$TMP/$base.log"; then
            nolink=$((nolink + 1))
            continue
        fi
        cerr=$((cerr + 1))
        [ $VERBOSE = 1 ] && printf '编不过   %-24s %s\n' "$base" "$(head -1 "$TMP/$base.log")"
        continue
    fi
    if [ ! -f "$TCCDIR/$base.expect" ]; then
        nolink=$((nolink + 1))
        continue
    fi
    ( cd "$TMP" && timeout -k 5 20 "./$base" >"$TMP/$base.out" 2>&1 )
    if cmp -s "$TCCDIR/$base.expect" "$TMP/$base.out"; then
        pass=$((pass + 1))
        [ $VERBOSE = 1 ] && printf '通过     %s\n' "$base"
    else
        # expect 文件本身过旧的：gcc 编出来的输出也对不上（上游也这么判）
        if command -v gcc >/dev/null 2>&1 \
           && gcc -w -o "$TMP/$base.gcc" "$src" >/dev/null 2>&1 \
           && ( cd "$TMP" && timeout -k 5 20 "./$base.gcc" >"$TMP/$base.gcc.out" 2>&1 ) \
           && ! cmp -s "$TCCDIR/$base.expect" "$TMP/$base.gcc.out"; then
            cstale=$((cstale + 1))
            [ $VERBOSE = 1 ] && printf 'expect旧 %-24s gcc 输出也对不上 expect\n' "$base"
        else
            fail=$((fail + 1))
            [ $VERBOSE = 1 ] && printf '输出不符 %-24s %s\n' "$base" \
                "$(diff "$TCCDIR/$base.expect" "$TMP/$base.out" 2>&1 | head -1)"
        fi
    fi
done
total=$((pass + fail + cerr + nolink + cstale))
echo "g2c tests2（tcc-c6c）：通过 $pass / 仅编译 $nolink / 输出不符 $fail / 编不过 $cerr / expect 旧 $cstale （共 $total）"
[ "$cerr" -eq 0 ] && [ "$fail" -eq 0 ]
