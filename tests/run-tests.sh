#!/bin/sh
# c6 自测运行器：跑 tests/ 下的所有 c6 测试。
#
#   tests/run-tests.sh                 跑全部
#   tests/run-tests.sh defer err       只跑这几个（按文件名，可写 errors/xxx）
#   tests/run-tests.sh --list          列出会跑的文件
#
# 环境变量：
#   TCC       编译器路径（默认 <仓库>/tcc/tcc）
#   TCCFLAGS  追加给编译器的参数
#
# 两类测试：
#   tests/*.c         用 -test 跑里面的 test 块；若存在同名的 tests/tu/<name>.c，
#                     会一起编译，作为第二个翻译单元。
#   tests/errors/*.c  编译期诊断；每个 `// EXPECT <片段>: <诊断>` 注释对应一次
#                     -D<片段> 编译，要求 stderr 含该诊断；不带 -D 时该文件
#                     必须能编译并运行通过。
#   tests/exit/*.c    退出码；`// EXIT <n>` 注释声明期望的进程退出码，编译成
#                     可执行文件后运行，比对退出码（main 的返回值必须穿过
#                     c6 的清理链）。
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TCC=${TCC:-$ROOT/tcc/tcc}
TCCFLAGS=${TCCFLAGS:-}
BASE="-B $ROOT/tcc -std=c6"

if [ ! -x "$TCC" ]; then
    echo "run-tests: 找不到 $TCC，先执行 'make build'" >&2
    exit 1
fi

TMP=${TMPDIR:-/tmp}/c6-tests.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

npass=0
nfail=0
failed_list=

fail() {
    nfail=$((nfail + 1))
    failed_list="$failed_list $1"
    echo "FAIL: $1" >&2
    shift
    [ $# -eq 0 ] || printf '%s\n' "$@" | sed 's/^/      /' >&2
    return 0
}

note_pass() {
    npass=$((npass + 1))
    echo "ok   $1"
}

# 用户点名要跑的测试（去掉 .c 后缀的纯名字），空表示全跑
wanted=
if [ "${1:-}" = "--list" ]; then
    for f in "$ROOT"/tests/*.c; do
        echo "$(basename "$f" .c)"
    done
    for f in "$ROOT"/tests/errors/*.c; do
        [ -e "$f" ] && echo "errors/$(basename "$f" .c)"
    done
    for f in "$ROOT"/tests/exit/*.c; do
        [ -e "$f" ] && echo "exit/$(basename "$f" .c)"
    done
    exit 0
fi
for a in "$@"; do
    wanted="$wanted $(echo "$a" | sed 's,\.c$,,; s,^\./,,; s,^tests/,,')"
done

is_wanted() {
    [ -z "$wanted" ] && return 0
    for w in $wanted; do
        case $1 in
        *"$w" | "$w" | "$w"/*) return 0 ;;
        esac
    done
    return 1
}

# ---- 1. test 块 ----
for src in "$ROOT"/tests/*.c; do
    name=$(basename "$src" .c)
    is_wanted "$name" || continue
    args="$TCC $BASE $TCCFLAGS -test $src"
    tu="$ROOT/tests/tu/$name.c"
    [ -e "$tu" ] && args="$args $tu"
    if out=$($args 2>"$TMP/err") && echo "$out" | tail -n 1 | grep -q ', 0 failure(s)$'; then
        note_pass "$name: $(echo "$out" | tail -n 1)"
    else
        fail "$name" "$(cat "$TMP/err")" "$(echo "$out" | grep -E '^(FAIL|[0-9]+ test)')"
    fi
done

# ---- 2. 编译期诊断 ----
for src in "$ROOT"/tests/errors/*.c; do
    [ -e "$src" ] || continue
    name="errors/$(basename "$src" .c)"
    is_wanted "$name" || continue

    # 期望表：片段名 <TAB> 诊断文本
    sed -n 's,^// *EXPECT *\([A-Za-z0-9_]*\): *\(.*\)$,\1\t\2,p' "$src" > "$TMP/expect"
    frags=$(cut -f1 "$TMP/expect" | awk '!seen[$0]++')

    ok=yes
    detail=

    # 2a. 每个片段都必须报出声明的全部诊断
    for frag in $frags; do
        if $TCC $BASE $TCCFLAGS -D"$frag" -run "$src" \
                2>"$TMP/err" >/dev/null; then
            ok=no
            detail="$detail
-D$frag: 没有报错"
        fi
        awk -F'\t' -v f="$frag" '$1 == f { print substr($0, length($1) + 2) }' \
            "$TMP/expect" |
        while IFS= read -r w; do
            [ -z "$w" ] && continue
            grep -qF -- "$w" "$TMP/err" || printf '%s\n' "-D$frag: 缺少诊断: $w"
        done > "$TMP/missing"
        if [ -s "$TMP/missing" ]; then
            ok=no
            detail="$detail
$(cat "$TMP/missing")"
        fi
    done

    # 2b. 不带 -D 时必须能编译并运行通过
    if ! out=$($TCC $BASE $TCCFLAGS -run "$src" 2>"$TMP/err"); then
        ok=no
        detail="$detail
正常路径编译或运行失败:$(cat "$TMP/err")$out"
    fi

    if [ $ok = yes ]; then
        note_pass "$name: $(echo "$frags" | wc -w | tr -d ' ') 个诊断片段"
    else
        fail "$name" "$detail"
    fi
done

# ---- 3. 退出码 ----
for src in "$ROOT"/tests/exit/*.c; do
    [ -e "$src" ] || continue
    name="exit/$(basename "$src" .c)"
    is_wanted "$name" || continue
    want=$(sed -n 's,^// *EXIT *\([0-9][0-9]*\)$,\1,p' "$src" | head -n 1)
    if [ -z "$want" ]; then
        fail "$name" "缺少 '// EXIT <n>' 注释"
        continue
    fi
    if ! $TCC $BASE $TCCFLAGS -o "$TMP/exit.bin" "$src" 2>"$TMP/err" >/dev/null; then
        fail "$name" "$(cat "$TMP/err")"
        continue
    fi
    "$TMP/exit.bin" >/dev/null 2>"$TMP/err"
    got=$?
    if [ "$got" = "$want" ]; then
        note_pass "$name: 退出码 $got"
    else
        fail "$name" "退出码 $got，期望 $want"
    fi
done

echo
echo "run-tests: $npass 通过, $nfail 失败"
if [ $nfail -ne 0 ]; then
    echo "run-tests: 失败:$failed_list" >&2
    exit 1
fi
exit 0
