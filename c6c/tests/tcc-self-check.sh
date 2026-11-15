#!/bin/sh
# 目标（2）的度量尺：用 **c6c 编出来的那个 tcc** 跑 tcc 自己的 tests2。
#
# 和 tests/tcc-tests2.sh 的区别：
#   tcc-tests2.sh   c6c 直接编用例（量的是 c6c 的前端+后端）
#   本脚本           c6c 编出 tcc，再由 tcc 编用例（量的是「c6c 能不能编一个
#                   能干活的 tcc」——G2c）
#
# 用法：tests/tcc-self-check.sh <tcc-binary> [-v] [用例名前缀]
set -u
TCCBIN=${1:?用法: tcc-self-check.sh <tcc-binary> [-v] [前缀]}
shift
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
TCCDIR=${TCCDIR:-$ROOT/tcc/tests/tests2}
VERBOSE=0
FILTER=""
for a in "$@"; do
    case "$a" in
    -v) VERBOSE=1 ;;
    *) FILTER=$a ;;
    esac
done
# 编译/运行的上限（秒）。c6c 编出来的 tcc 要编 tcc.c 本体（2.5 MB），
# 给得宽一点；目的只有一个：卡死的时候别让 build-tcc（check-all 的最后一步）
# 永久挂在那里。`timeout -k` = 先 SIGTERM，几秒后还在就 SIGKILL。
TMO=${TMO:-300}
RUN_TMO=${RUN_TMO:-30}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0
cerr=0
for src in "$TCCDIR"/*.c; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .c)
    case "$base" in
    *"$FILTER"*) ;;
    *) continue ;;
    esac
    args=""
    case "$base" in
    31_args) args="arg1 arg2 arg3 arg4 arg5" ;;
    esac
    # 在用例目录里用**文件名**编：编译期告警里的路径要和上游 .expect 一致
    # （`03_struct.c:14: …` 而不是 `tcc/tests/tests2/03_struct.c:14: …`）。
    if ! timeout -k 5 "$TMO" sh -c "cd '$TCCDIR' && exec; \"\$0\" -B'$ROOT/tcc' -I'$ROOT/tcc/include' \
        -o '$TMP/$base' '$base.c' >'$TMP/$base.log' 2>&1" "$TCCBIN"; then
        cerr=$((cerr + 1))
        [ "$VERBOSE" = 1 ] && printf '编不过 %-22s %s\n' "$base" "$(head -1 "$TMP/$base.log")"
        continue
    fi
    exp="$TCCDIR/$base.expect"
    [ -e "$exp" ] || exp=/dev/null
    # 编译期的**告警**要先进输出：上游的 .expect 里就是这么排的
    # （`03_struct.expect` 第一行就是那条 `__cleanup__ ignored on type`）。
    : > "$TMP/$base.out"
    if [ -s "$TMP/$base.log" ]; then
        grep -v '^$' "$TMP/$base.log" >> "$TMP/$base.out" 2>/dev/null
    fi
    # `) 2>/dev/null` 只关掉**这个子 shell 自己**的 stderr：被信号打死的用例
    # 会打一行 "Segmentation fault"，那不是记分板的内容；程序的 stderr 上面
    # 已经显式并进 .out 了。
    ( cd "$TMP" && timeout -k 2 "$RUN_TMO" "./$base" $args \
        >>"$TMP/$base.out" 2>&1 ) 2>/dev/null
    if diff -q "$exp" "$TMP/$base.out" >/dev/null 2>&1; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        [ "$VERBOSE" = 1 ] && printf '输出不符 %-22s %s\n' "$base" \
            "$(diff "$exp" "$TMP/$base.out" 2>&1 | head -1)"
    fi
done
total=$((pass + fail + cerr))
echo "c6c-built tcc 跑 tests2：通过 $pass / 输出不符 $fail / 编不过 $cerr （共 $total）"
# 地板线：防止「改坏了但只要有 1 个过就算通过」。目前实测 109，留点余量。
MIN=${C6C_TCC_SELF_MIN:-100}
[ "$pass" -ge "$MIN" ] || { echo "c6c: 低于地板线 $MIN"; exit 1; }

# 第三代：拿这个 tcc **再编一遍 tcc.c**（就是 tcc 自己的 selfhost 路径），再让
# 第三代跑几条用例。"c6c 能编出一个能干活的 tcc"到这里才闭环：能编别人的代码，
# 也能编它自己。
if [ -z "$FILTER" ]; then
    g2="$TMP/tcc-g2"
    if timeout -k 10 "$TMO" "$TCCBIN" -B"$ROOT/tcc" -DONE_SOURCE=1 -I "$ROOT/tcc" \
            -o "$g2" "$ROOT/tcc/tcc.c" >"$TMP/g2.log" 2>&1 && [ -x "$g2" ]; then
        n=0
        bad=0
        for base in 01_comment 27_sizeof 73_arm64 90_struct-init; do
            [ -f "$TCCDIR/$base.c" ] || continue
            n=$((n + 1))
            ( cd "$TCCDIR" && timeout -k 5 "$TMO" "$g2" -B"$ROOT/tcc" "$base.c" \
                -o "$TMP/g2-$base" ) >"$TMP/g2-$base.log" 2>&1 || bad=$((bad + 1))
            ( cd "$TMP" && timeout -k 2 "$RUN_TMO" "./g2-$base" >"$TMP/g2-$base.out" 2>&1 )
            cmp -s "$TCCDIR/$base.expect" "$TMP/g2-$base.out" || bad=$((bad + 1))
        done
        echo "c6c-built tcc 再编一遍 tcc.c（第三代 selfhost）：编出 OK / 抽查 $n 条用例，坏 $bad"
        [ "$bad" -eq 0 ] || exit 1
    else
        echo "c6c: 第三代 selfhost 失败（c6c 编出的 tcc 编不了 tcc.c）"
        sed -n '1,3p' "$TMP/g2.log" 2>/dev/null
        exit 1
    fi
fi
