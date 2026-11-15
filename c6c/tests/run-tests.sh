#!/bin/sh
# tests/ 的两把尺子：
#   ① 前端：每个 *.c6 都要过 -fsyntax-only（诊断通道也要干净）。
#   ② 语义：**真的把 test 块跑一遍**。没有 main 的文件由 c6c 合成入口
#      （synth_test_main），失败数当退出码。
# 以前只做 ①，等于一条断言都没跑过；② 才是"1157 个 test 块"那条验收的口径。
#
# 并行：`-j N`（或环境变量 JOBS=N）用 N 个子进程同时编/跑。每个用例是**独立
# 的编译器进程**（互不共享状态），所以并行只改变完成的先后，不改变结论；汇总
# 时按文件序（find|sort）读结果文件，报告仍然逐字节稳定。
set -u

C6C=${C6C:-./build/c6c}
ROOT=${ROOT:-..}
JOBS=${JOBS:-1}
# 用例运行时统一按 **Linux 默认栈**（8 MB）跑：开发机常见 64 MB，会把"深递归
# 吃栈"这类问题藏到 CI（8~16 MB）才炸一次——gc_field 的 20001 节点长链释放、
# defer_scope 的深递归都这么在 CI 上失败过。C6C_TEST_STACK=0 关掉这项限制；
# 非 Linux 平台自动关掉（`ulimit -s` 的单位不一样）。
STACK_KB=${C6C_TEST_STACK:-8192}
case "$(uname -s 2>/dev/null)" in
Linux) ;;
*) STACK_KB=0 ;;
esac
case "$STACK_KB" in
''|*[!0-9]*) STACK_KB=8192 ;;
esac
[ "$STACK_KB" -ge 1 ] 2>/dev/null || STACK_KB=0
while [ $# -gt 0 ]; do
    case "$1" in
    -j) shift; [ $# -gt 0 ] && JOBS=$1 ;;
    -j*) JOBS=${1#-j} ;;
    esac
    shift
done
case "$JOBS" in
''|*[!0-9]*) JOBS=1 ;;
esac
[ "$JOBS" -ge 1 ] || JOBS=1

TMP=${TMPDIR:-/tmp}/c6c-tests.$$
mkdir -p "$TMP" || exit 1
trap 'rm -rf "$TMP"' EXIT

# 一个用例的全部工作：前端 + 编译 + 跑。结论写进 $TMP/v.<序号>（两行，
# 第一行前端口径、第二行运行口径），由驱动按序汇总——不要在这里打印，
# 并行时输出会互相穿插。
one_file() {
    f=$1
    i=$2
    tmo=${3:-20}
    out="$TMP/v.$i"

    : >"$out"
    if ! "$C6C" -fsyntax-only -I"$ROOT" -I"$ROOT/std" -I"$ROOT/tests" "$f" >/dev/null 2>&1; then
        echo "FBAD" >>"$out"
        echo "SKIP" >>"$out"
        return
    fi
    echo "FOK" >>"$out"
    n=$(echo "$f" | tr '/' '_')
    case "$f" in
    */tests/tu/*) echo "TU" >>"$out"; return ;;
    # tests/errors/ 是**诊断**用例：按 `// EXPECT <片段名>: error: …` 分片段
    # 用 -D 编，由 tests/diag-tests.sh 负责。整文件编是它的"正常路径"，不带
    # -D 本来就该过，拿它当语义用例跑只会误报。
    */tests/errors/*) echo "SKIP" >>"$out"; return ;;
    esac
    extra=""
    # tests/abi/ 是「c6 侧 + 纯 C 侧」成对写的 ABI 用例（`X.c6` 配
    # `X_plain.c`）：纯 C 那一半看不见 c6 的类型，两边只共享机器字级别的
    # ABI，必须一起编——单独编 `X.c6` 只会缺 `plain_*` 符号。
    plain=""
    case "$f" in
    */tests/abi/*) plain="$ROOT/tests/abi/$(basename "$f" .c6)_plain.c" ;;
    esac
    if [ -f "$plain" ]; then
        extra="$plain"
    elif [ -f "$ROOT/tests/tu/$(basename "$f")" ] && [ "$f" != "$ROOT/tests/tu/$(basename "$f")" ]; then
        extra="$ROOT/tests/tu/$(basename "$f")"
    fi
    if ! "$C6C" -I"$ROOT" -I"$ROOT/std" -I"$ROOT/tests" "$f" $extra -o "$TMP/$n" >"$TMP/$n.log" 2>&1; then
        if grep -q '没有 main' "$TMP/$n.log"; then
            # 纯模块文件（只有 module/import 和导出函数）：上游也只编不跑
            echo "NOLINK" >>"$out"
            return
        fi
    fi
    if [ ! -x "$TMP/$n" ]; then
        echo "RBAD $(basename "$f" .c6)" >>"$out"
        return
    fi
    # 超时保护：并发相关的用例在"同步线程桩"下可能死等，别把整个套件拖住
    if [ "$STACK_KB" -gt 0 ]; then
        # 见文件头：按 Linux 默认栈跑，别让开发机的大栈掩盖深递归问题
        ( ulimit -s "$STACK_KB" 2>/dev/null; timeout "$tmo" "$TMP/$n" ) \
            >>"$TMP/$n.log" 2>&1
        rc=$?
    else
        timeout "$tmo" "$TMP/$n" >>"$TMP/$n.log" 2>&1
        rc=$?
    fi
    case "$f" in
    */tests/exit/*)
        # tests/exit/*：**退出码就是断言**。文件里写了 `// EXIT n` 就按它比，
        # 没写就要求非 0（历史默认）。
        want=$(grep -m1 '^// EXIT ' "$f" 2>/dev/null | awk '{print $3}')
        if [ -n "$want" ]; then
            if [ "$rc" = "$want" ]; then echo "ROK" >>"$out"; return; fi
        elif [ "$rc" -ne 0 ]; then
            echo "ROK" >>"$out"
            return
        fi
        ;;
    *)
        if [ "$rc" -eq 0 ]; then echo "ROK" >>"$out"; return; fi
        ;;
    esac
    echo "RBAD $(basename "$f" .c6)" >>"$out"
}

# 并发/协程那批用例对**机器负载**敏感（它们自己会 futex 自旋、还有内部轮次
# 上限），并行跑会把它们拖成超时/失败——不是被测行为变了，是测量方式变了。
# 所以这些单独走串行那一遍，其余并行。判据按文件名（bsae 里带 async/thread/
# spawn/task/co/gc_thread 的）。
is_serial() {
    case "$(basename "$1")" in
    *async*|*thread*|*spawn*|*task*|*co_*|*go_join*) return 0 ;;
    esac
    return 1
}

i=0
# ① 并行那一遍（跳过上面那类）
for f in $(find "$ROOT/tests" -name '*.c6' | sort); do
    i=$((i + 1))
    if [ "$JOBS" -le 1 ] || is_serial "$f"; then
        continue
    fi
    one_file "$f" "$i" &
    # 没有 `wait -n`（POSIX）：攒够一批就整批等，粗粒度但确定
    if [ "$(jobs -p | wc -l)" -ge "$JOBS" ]; then
        wait
    fi
done
wait
# ② 串行那一遍（并发/协程用例 + JOBS=1 时的全部）
i=0
for f in $(find "$ROOT/tests" -name '*.c6' | sort); do
    i=$((i + 1))
    if [ "$JOBS" -gt 1 ] && ! is_serial "$f"; then
        continue
    fi
    # 并发/协程用例在机器有别的负载时会慢很多：超时放宽到 90 s，
    # 免得把"负载高"误判成"用例失败"。
    if is_serial "$f"; then
        one_file "$f" "$i" 90
    else
        one_file "$f" "$i"
    fi
done

ok=0
bad=0
first=""
rok=0
nolink=0
rbad=0
rfirst=""
rlist=""
ntu=0
idx=0
for f in $(find "$ROOT/tests" -name '*.c6' | sort); do
    idx=$((idx + 1))
    v="$TMP/v.$idx"
    [ -f "$v" ] || continue
    fl=$(sed -n '1p' "$v")
    rl=$(sed -n '2p' "$v")
    if [ "$fl" = "FBAD" ]; then
        bad=$((bad + 1))
        [ -z "$first" ] && first=$f
        continue
    fi
    ok=$((ok + 1))
    case "$rl" in
    TU) ntu=$((ntu + 1)) ;;
    NOLINK) nolink=$((nolink + 1)) ;;
    ROK) rok=$((rok + 1)) ;;
    RBAD*) rbad=$((rbad + 1)); [ -z "$rfirst" ] && rfirst=$f
        rlist="$rlist ${rl#RBAD }" ;;
    esac
done

echo "c6c: tests 前端 通过 $ok / 失败 $bad"
echo "c6c: tests 跑通 通过 $rok / 仅编译 $nolink / 失败 $rbad / 第二 TU $ntu"
if [ "$bad" -ne 0 ]; then
    echo "c6c: 第一个前端失败样例：$first"
fi
if [ "$rbad" -ne 0 ]; then
    echo "c6c: 跑不通的：$(echo "$rlist" | tr ' ' '\n' | sort | uniq | tr '\n' ' ')"
fi
[ "$bad" -eq 0 ] && [ "$rbad" -eq 0 ]
