#!/bin/sh
# 三后端同一套用例（验收第 1 条）：同一份 C 源，native 后端和 LLVM IR 后端
# 各编一遍，比**标准输出 + 退出码**，不是只看"编过了"。
#
# LLVM 后端出的是文本 .ll，后面还要 llc 出目标文件、cc 链接（-no-pie：
# 我们发的地址是绝对重定位）。和 C99 后端一样，这一步是它和 native 的
# 本质区别：native 直出 ELF，LLVM 出的是可优化的 IR。
set -u

C6C=${C6C:-./build/c6c}
LLC=${LLC:-llc}
CC=${CC:-gcc}
CORPUS=${CORPUS:-tests/c}
TMP=${TMPDIR:-/tmp}/c6c-llvmdiff.$$
mkdir -p "$TMP" || exit 1
trap 'rm -rf "$TMP"' EXIT

ok=0
bad=0
badlist=""
# 只有 native 后端能跑的用例（`native-only.txt` 一行一个文件名）：
# 目前是 `__thread`——C99/LLVM 后端还没发"构造函数里调 c6c_tls_setup()"那条，
# 跑起来会踩空 %fs 基址。等三后端都支持了就把这个文件删掉。
for f in "$CORPUS"/*.c; do
    name=$(basename "$f" .c)
    if [ -f "$CORPUS/llvm-only.txt" ] && grep -qx "$name" "$CORPUS/llvm-only.txt"; then
        skip=$((skip + 1))
        continue
    fi
    if [ -f "$CORPUS/native-only.txt" ] && grep -qx "$name" "$CORPUS/native-only.txt"; then
        continue
    fi
    if ! "$C6C" "$f" -o "$TMP/$name.n" >"$TMP/$name.nlog" 2>&1; then
        bad=$((bad + 1)); badlist="$badlist $name(native-cc)"; continue
    fi
    if ! "$C6C" --emit-llvm "$f" -o "$TMP/$name.ll" >"$TMP/$name.llog" 2>&1; then
        bad=$((bad + 1)); badlist="$badlist $name(llvm-emit)"; continue
    fi
    if ! "$LLC" -O0 -filetype=obj -o "$TMP/$name.o" "$TMP/$name.ll" >>"$TMP/$name.llog" 2>&1; then
        bad=$((bad + 1)); badlist="$badlist $name(llc)"; continue
    fi
    if ! "$CC" -no-pie -o "$TMP/$name.x" "$TMP/$name.o" >>"$TMP/$name.llog" 2>&1; then
        bad=$((bad + 1)); badlist="$badlist $name(link)"; continue
    fi
    "$TMP/$name.n" >"$TMP/$name.nout" 2>&1; nrc=$?
    "$TMP/$name.x" >"$TMP/$name.xout" 2>&1; xrc=$?
    if [ "$nrc" -ne "$xrc" ]; then
        bad=$((bad + 1)); badlist="$badlist $name(rc $nrc!=$xrc)"; continue
    fi
    if ! cmp -s "$TMP/$name.nout" "$TMP/$name.xout"; then
        bad=$((bad + 1)); badlist="$badlist $name(out)"; continue
    fi
    ok=$((ok + 1))
done

echo "c6c: LLVM IR 后端 vs native 后端（$CORPUS）：一致 $ok / 不一致 $bad"
if [ "$bad" -ne 0 ]; then
    echo "c6c: 不一致项：$badlist"
    # 失败原因都在每例的 .llog 里（llc / cc 的 stderr）。以前只在临时目录里
    # 留着、trap 一删就没了，CI 上只能看到 "(link)" 这种分类，等于没信息；
    # 这里把最后一例的日志尾巴打出来（真要逐例看得自己跑一遍 -v 口径）。
    for f in "$CORPUS"/*.c; do
        n=$(basename "$f" .c)
        case " $badlist " in
        *" $n("*)
            echo "---- $n 的日志（最后 6 行）"
            tail -6 "$TMP/$n.llog" 2>/dev/null
            ;;
        esac
    done | head -80
    exit 1
fi
