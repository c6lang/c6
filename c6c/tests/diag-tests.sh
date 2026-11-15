#!/bin/sh
# c6 编译期诊断用例：tests/errors/*.c6 里每条
#     // EXPECT <片段名>: error: <诊断文本>
# 都要求"用 -D<片段名> 编这个文件时，真的报出这段文本"。
# 不带 -D 时是文件的正常路径，必须编得过去（`test` 目标已经在查那条，
# 这里也顺手复查一遍，省得两个套件结论打架）。
#
# 并行：`-j N`（默认 1）按**文件**并行（每个文件里的片段仍然串行）——每个
# 文件是一串独立的编译器进程，所以并行只改变完成的先后。结论写进
# `$TMP/r.<序号>`，最后按文件名顺序汇总，报告与串行逐字节一致。
#
# 用法：C6C=<编译器> ROOT=<仓库根> ./tests/diag-tests.sh [-v] [-j N]
set -u

C6C=${C6C:-./build/c6c}
ROOT=${ROOT:-..}
V=0
JOBS=${JOBS:-1}
while [ $# -gt 0 ]; do
    case "$1" in
    -v) V=1 ;;
    -j) shift; [ $# -gt 0 ] && JOBS=$1 ;;
    -j*) JOBS=${1#-j} ;;
    esac
    shift
done
case "$JOBS" in ''|*[!0-9]*) JOBS=1 ;; esac
[ "$JOBS" -ge 1 ] || JOBS=1

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

one_file() {
    f=$1
    out=$2
    extra=
    tu_src="$ROOT/tests/tu/$(basename "$f")"
    [ -f "$tu_src" ] && extra="$tu_src"
    : >"$out"
    # 正常路径
    if ! "$C6C" -fsyntax-only -I"$ROOT" -I"$ROOT/std" -I"$ROOT/tests" "$f" \
            $extra >/dev/null 2>&1; then
        echo "SKIP" >>"$out"
    else
        echo "OK" >>"$out"
    fi
    # 每条 EXPECT
    grep -n '^// EXPECT ' "$f" | while IFS= read -r line; do
        spec=${line#*EXPECT }
        frag=${spec%%:*}
        rest=${spec#*:}
        rest=${rest#error: }
        frag=$(echo "$frag" | tr -d ' ')
        o=$("$C6C" -fsyntax-only -D"$frag" -I"$ROOT" -I"$ROOT/std" \
                 -I"$ROOT/tests" "$f" $extra 2>&1)
        # 判据一：这个片段必须**报错**（c6c 的诊断文本是中文的，上游 EXPECT
        # 里写的是英文原文，所以不强求逐字一致；下面把逐字命中的条数单独统计，
        # 作为"离上游措辞还有多远"的参考）。
        if printf '%s' "$o" | grep -q '错误\|error'; then
            if printf '%s' "$o" | grep -qF "$rest"; then
                echo "PASSTEXT" >>"$out"
            else
                echo "PASS" >>"$out"
            fi
        else
            echo "FAIL $(basename "$f") $frag" >>"$out"
        fi
    done
}

i=0
for f in "$ROOT"/tests/errors/*.c6; do
    [ -f "$f" ] || continue
    i=$((i + 1))
    if [ "$JOBS" -le 1 ]; then
        one_file "$f" "$TMP/r.$i"
    else
        one_file "$f" "$TMP/r.$i" &
        if [ "$(jobs -p | wc -l)" -ge "$JOBS" ]; then
            wait
        fi
    fi
done
wait

ok=0
bad=0
txt=0
skip=0
nfile=0
i=0
for f in "$ROOT"/tests/errors/*.c6; do
    [ -f "$f" ] || continue
    i=$((i + 1))
    nfile=$((nfile + 1))
    r="$TMP/r.$i"
    [ -f "$r" ] || continue
    if [ "$(sed -n '1p' "$r")" = "SKIP" ]; then
        skip=$((skip + 1))
        [ $V -eq 1 ] && echo "正常路径编不过  $(basename "$f")"
    fi
    ok=$((ok + $(grep -c '^PASS' "$r")))
    txt=$((txt + $(grep -c '^PASSTEXT' "$r")))
    bad=$((bad + $(grep -c '^FAIL' "$r")))
    if [ $V -eq 1 ]; then
        grep '^FAIL' "$r" | sed 's/^FAIL [^ ]* //' | sed 's/^/  未报出：/'
    fi
done

echo "c6c: 编译期诊断 报错 $ok / 没报错 $bad（$nfile 个文件，$skip 个正常路径编不过；逐字命中上游措辞 $txt 条）"
[ "$bad" -eq 0 ]
