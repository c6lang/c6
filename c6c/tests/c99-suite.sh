#!/bin/sh
# 语言层 C99 外部闸门：c-testsuite（third_party/c-testsuite/，scc 来源 150 条）。
#
# 上游接口（tests/single-exec）：编一个可执行 → 跑 → 程序 stdout+stderr 必须
# 逐字节等于 *.c.expected，且退出码 0。这里照抄这个口径，只加分类：
#
#   PASS   与 .expected 完全一致
#   LANG   前端编不过（语法/语义/诊断问题）—— 这是"语言 100% 兼容 C99"要清掉的
#   OUT    编过了但跑出来不一样 —— 可能是代码生成错，也可能是库行为差异（见下）
#   LIB    链接期缺符号（运行期没实现）—— 按"库不管"的口径，这类不算语言问题，
#          但单独计数，免得和代码生成错混在一起
#   TMO    c6c 编译这一条超时（编译期卡死）—— 挡闸门：卡死比报错更糟，不能当成
#          "库里没这个符号"或"输出不符"混过去
#   MISS   这一条没留下任何结论（子进程被打死/结论文件为空）—— 也挡闸门，
#          否则一条挂掉的用例会从总数里消失，闸门反而变绿
#   STALE  上游 .expected 本身过期：拿 gcc -std=c99 编同一份、跑出来也对不上。
#          这类是套件自己的账，不算 c6c 的
#   SKIP   gcc -std=c99 都编不过（用例不是合法 C99，常见于故意写错的负例）
#   N/A    超出口径的用例（C11 才有的语言特性等），清单在 tests/c99-out-of-scope.txt，
#          每条都写了具体卡点。它们照样跑、照样显示，只是不挡闸门
#          （但如果它们 TMO/MISS，照样挡——那是 c6c 卡死，不是口径问题）
#
# 判定顺序保证了"不是 c6c 的错"先被摘出去：先用 gcc 当 oracle，再把 c6c 的
# 失败按 LANG/LIB 分开，最后才比输出。
#
# 卡死保护（这就是 `make check-all` 以前会挂住的那条路）：每条用例的**编译器
# 调用**和**跑程序**都有上限，而且一律是 `timeout -k`——先 SIGTERM，过几秒还在
# 就 SIGKILL。只用 `timeout` 的话，一个不理 SIGTERM 的用例（或者被自己装的处理
# 函数接住的信号）就能把整个验收永久挂住。
#
# 用法：
#   tests/c99-suite.sh [-v] [-j N] [用例名前缀]
#   C6C=build/c6c ROOT=$PWD ./tests/c99-suite.sh
#   CASE_TMO=300 RUN_TMO=30 tests/c99-suite.sh   # 慢机器上放宽上限（秒）
#
# 退出码：LANG / OUT / TMO / MISS 任一非 0 时退出 1（STALE/SKIP/LIB 不挡）。
# LIB 单列是因为目标口径是"语言层 100%，库不管"：库缺符号不应该掩盖语言层的红。
set -u

C6C=${C6C:-build/c6c}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=${ROOT:-$HERE/../..}
SUITE=${SUITE:-$ROOT/third_party/c-testsuite/single-exec}
ORACLE_CC=${ORACLE_CC:-gcc}
OOS=${OOS:-$HERE/c99-out-of-scope.txt}
# 上限（秒）：CASE_TMO 管"编一条用例"（c6c 或 gcc），RUN_TMO 管"跑一下"。
# 卡死的时候是 timeout 出手，不是整个套件陪着等。
CASE_TMO=${CASE_TMO:-120}
RUN_TMO=${RUN_TMO:-10}
VERBOSE=0
FILTER=""
JOBS=${JOBS:-1}

while [ $# -gt 0 ]; do
    case "$1" in
    -v) VERBOSE=1 ;;
    -j) shift; [ $# -gt 0 ] && JOBS=$1 ;;
    -j*) JOBS=${1#-j} ;;
    *) FILTER=$1 ;;
    esac
    shift
done
case "$JOBS" in ''|*[!0-9]*) JOBS=1 ;; esac
[ "$JOBS" -ge 1 ] || JOBS=1
case "$CASE_TMO" in ''|*[!0-9]*) CASE_TMO=120 ;; esac
[ "$CASE_TMO" -ge 1 ] || CASE_TMO=120
case "$RUN_TMO" in ''|*[!0-9]*) RUN_TMO=10 ;; esac
[ "$RUN_TMO" -ge 1 ] || RUN_TMO=10
# 124 = 超时（TERM 打死了），137 = 过了宽限期还在、被 KILL 打死
is_timeout() { [ "$1" -eq 124 ] || [ "$1" -eq 137 ]; }

if [ ! -d "$SUITE" ]; then
    echo "找不到用例目录：$SUITE" >&2
    echo "（先确认 third_party/c-testsuite 在仓库里；ROOT=$ROOT）" >&2
    exit 2
fi
if [ ! -x "$C6C" ]; then
    echo "找不到可执行的 C6C：$C6C（先 make build）" >&2
    exit 2
fi

# 超出口径的用例清单（tests/c99-out-of-scope.txt：`用例名 原因`）
oos_has() {
    [ -f "$OOS" ] || return 1
    grep -q "^$1 " "$OOS"
}
oos_reason() {
    grep "^$1 " "$OOS" | sed "s/^$1 //"
}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# 每条用例的结论写进 $TMP/v.<序号>（一行），并行时不打乱计数器。
# 每一条从一开始就有结论（MISS）：子进程要是被打死，结论文件留着 MISS，
# 汇总时它照样出现在总数里挡闸门，不会悄悄从统计里消失。
one_case() {
    src=$1
    vf=$2
    base=$(basename "$src" .c)
    exp="$src.expected"
    [ -e "$exp" ] || exp=/dev/null
    : >"$vf"

    # 1) gcc 当 oracle：编不过 ⇒ 用例不是合法 C99
    timeout -k 5 "$CASE_TMO" "$ORACLE_CC" -std=c99 -w -o "$TMP/g_$base" "$src" \
        >"$TMP/g_$base.log" 2>&1
    orc=$?
    if [ "$orc" -ne 0 ]; then
        if is_timeout "$orc"; then
            echo "SKIP oracle 编译超时（>${CASE_TMO}s）" >"$vf"
        else
            echo "SKIP oracle 编不过" >"$vf"
        fi
        return
    fi
    # 2) oracle 跑出来的输出对不上 .expected（或退出码非 0）⇒ 上游 expect 过期
    #    协议要求"成功时退出码 0"，所以退出码也是判据的一部分。
    : >"$TMP/g_$base.out"
    ( cd "$TMP" && timeout -k 2 "$RUN_TMO" "./g_$base" >>"$TMP/g_$base.out" 2>&1 )
    grc=$?
    if is_timeout "$grc"; then
        echo "STALE oracle 运行超时（>${RUN_TMO}s，用例本身不终止）" >"$vf"
        return
    fi
    if [ "$grc" -ne 0 ]; then
        echo "STALE oracle 退出码 $grc（用例本身不满足“成功即 0”）" >"$vf"
        return
    fi
    if ! diff -q "$exp" "$TMP/g_$base.out" >/dev/null 2>&1; then
        echo "STALE $(diff "$exp" "$TMP/g_$base.out" 2>&1 | head -2 | tr '\n' ' ')" >"$vf"
        return
    fi

    # 3) c6c 编：卡死（TMO）单列，缺符号按 LIB 记，其余是语言层失败
    timeout -k 5 "$CASE_TMO" "$C6C" "$src" -o "$TMP/c_$base" \
        >"$TMP/c_$base.log" 2>"$TMP/c_$base.err"
    crc=$?
    if [ "$crc" -ne 0 ]; then
        if is_timeout "$crc"; then
            echo "TMO c6c 编译超时（>${CASE_TMO}s，编译器卡死）" >"$vf"
        elif grep -q '未定义的符号\|undefined symbol' "$TMP/c_$base.err"; then
            echo "LIB $(grep -o "未定义的符号 '[^']*'" "$TMP/c_$base.err" | head -1)" >"$vf"
        else
            echo "LANG $(head -1 "$TMP/c_$base.err")" >"$vf"
        fi
        return
    fi

    # 4) 跑：stdout+stderr 合并后与 .expected 比，且退出码必须是 0
    : >"$TMP/c_$base.out"
    ( cd "$TMP" && timeout -k 2 "$RUN_TMO" "./c_$base" >>"$TMP/c_$base.out" 2>&1 )
    crc=$?
    if is_timeout "$crc"; then
        echo "OUT 运行超时（>${RUN_TMO}s，编出来的程序不终止）" >"$vf"
        return
    fi
    if [ "$crc" -ne 0 ]; then
        # 死法要说清楚：139 = SIGSEGV，其余照抄
        echo "OUT 退出码 $crc（输出$(diff -q "$exp" "$TMP/c_$base.out" >/dev/null 2>&1 && echo "对得上" || echo "也对不上")）" >"$vf"
        return
    fi
    if diff -q "$exp" "$TMP/c_$base.out" >/dev/null 2>&1; then
        echo "PASS" >"$vf"
    else
        echo "OUT $(diff "$exp" "$TMP/c_$base.out" 2>&1 | head -2 | tr '\n' ' ')" >"$vf"
    fi
}

i=0
nrun=0
for src in "$SUITE"/*.c; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .c)
    case "$base" in
    *"$FILTER"*) ;;
    *) continue ;;
    esac
    i=$((i + 1))
    # 先按"没结论"占位：子进程要是被信号打死，这一条仍然留在总数里挡闸门。
    echo "MISS 结论缺失（子进程没跑完就被打死了？）" >"$TMP/v.$i"
    if [ "$JOBS" -le 1 ]; then
        one_case "$src" "$TMP/v.$i"
    else
        one_case "$src" "$TMP/v.$i" &
        # 限流的计数只能自己数：`$(jobs -p | wc -l)` 在 dash（Ubuntu 的
        # /bin/sh）里恒为 0——命令替换是子 shell，看不见父 shell 的作业表。
        # 以前那一版在 dash 下等于**没有限流**：150 条用例一次性全起来
        # （每条还带 gcc + c6c），小机器上直接把整机拖死，`-j` 形同虚设。
        nrun=$((nrun + 1))
        if [ "$nrun" -ge "$JOBS" ]; then
            wait
            nrun=0
        fi
    fi
done
wait

pass=0
lang=0
out=0
lib=0
stale=0
skip=0
na=0
tmo=0
miss=0
i=0
for src in "$SUITE"/*.c; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .c)
    case "$base" in
    *"$FILTER"*) ;;
    *) continue ;;
    esac
    i=$((i + 1))
    v="$TMP/v.$i"
    vl=$(cat "$v" 2>/dev/null)
    [ -n "$vl" ] || vl="MISS 结论文件不存在或为空（用例没跑出结论）"
    tags=""
    [ -f "$src.tags" ] && tags=$(tr '\n' ',' <"$src.tags" | sed 's/,$//')
    if oos_has "$base"; then
        case "$vl" in
        PASS*) [ "$VERBOSE" = 1 ] && \
            printf 'N/A    %-8s 已经过了，可以从 c99-out-of-scope.txt 里删掉\n' "$base" ;;
        # 超出口径的意思是"这条红不算语言层的账"，但**卡死不算红**：
        # c6c 在这条上编译超时/没结论，是编译器的问题，照样挡闸门。
        TMO*) tmo=$((tmo + 1))
           printf 'TMO    %-8s %s（超出口径的用例也卡死了）\n' "$base" "${vl#TMO }" ;;
        MISS*) miss=$((miss + 1))
           printf 'MISS   %-8s %s\n' "$base" "${vl#MISS }" ;;
        *) na=$((na + 1))
           printf 'N/A    %-8s %s\n' "$base" "${vl}"
           printf '       卡点：%s\n' "$(oos_reason "$base")" ;;
        esac
        continue
    fi
    case "$vl" in
    PASS*) pass=$((pass + 1)) ;;
    LANG*) lang=$((lang + 1))
        printf 'LANG   %-8s %s\n' "$base" "${vl#LANG }"
        [ -n "$tags" ] && printf '       tags: %s\n' "$tags" ;;
    OUT*) out=$((out + 1))
        printf 'OUT    %-8s %s\n' "$base" "${vl#OUT }"
        [ -n "$tags" ] && printf '       tags: %s\n' "$tags" ;;
    TMO*) tmo=$((tmo + 1))
        printf 'TMO    %-8s %s\n' "$base" "${vl#TMO }"
        [ -n "$tags" ] && printf '       tags: %s\n' "$tags" ;;
    MISS*) miss=$((miss + 1))
        printf 'MISS   %-8s %s\n' "$base" "${vl#MISS }" ;;
    LIB*) lib=$((lib + 1))
        [ "$VERBOSE" = 1 ] && printf 'LIB    %-8s %s\n' "$base" "${vl#LIB }" ;;
    STALE*) stale=$((stale + 1))
        [ "$VERBOSE" = 1 ] && printf 'STALE  %-8s %s\n' "$base" "${vl#STALE }" ;;
    SKIP*) skip=$((skip + 1))
        [ "$VERBOSE" = 1 ] && printf 'SKIP   %-8s %s\n' "$base" "${vl#SKIP }" ;;
    *) miss=$((miss + 1))
        printf 'MISS   %-8s 结论文件是空的（用例没跑出结论）\n' "$base" ;;
    esac
done

total=$((pass + lang + out + tmo + miss + lib + stale + skip + na))
echo
echo "c-testsuite（语言层 C99）：通过 $pass / 语言层失败 $lang / 输出不符 $out" \
    "/ 编译卡死 $tmo / 结论缺失 $miss" \
    "/ 库缺符号 $lib / 上游 expect 旧 $stale / 用例不成立 $skip / 超出口径 $na（共 $total）"
if [ "$lang" -ne 0 ] || [ "$out" -ne 0 ] || [ "$tmo" -ne 0 ] || [ "$miss" -ne 0 ]; then
    echo "闸门：红（LANG+OUT = $((lang + out))，TMO+MISS = $((tmo + miss))；语言层 C99 还没到 100%）"
    exit 1
fi
echo "闸门：绿（语言层 100%：没有 LANG/OUT/TMO/MISS）"
exit 0
