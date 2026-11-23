#!/bin/sh
# 三后端同一套用例（验收第 1 条）：同一份 C 源，native 后端和 C99 后端
# 各编一遍，比**标准输出 + 退出码**，不是只看"编过了"。
#
# C99 后端出的是 C 源文件，还要过一遍宿主 cc——这是它和 native 后端的
# 本质区别：native 直出 ELF，C99 出可移植的 C。
set -u

C6C=${C6C:-./build/c6c}
CC=${CC:-gcc}
CORPUS=${CORPUS:-tests/c}
# 上限（秒）：TMO 管"编一遍"（c6c / 宿主 cc），RUN_TMO 管"跑一下"。
# 一条用例卡死不该把整轮验收挂住；`timeout -k` 保证不理 SIGTERM 的也收得掉。
TMO=${TMO:-120}
RUN_TMO=${RUN_TMO:-60}
TMP=${TMPDIR:-/tmp}/c6c-c99diff.$$
mkdir -p "$TMP" || exit 1
trap 'rm -rf "$TMP"' EXIT

ok=0
bad=0
skip=0
badlist=""
# 只有 native 后端能跑的用例（`native-only.txt` 一行一个文件名）：
# 目前是 `__thread`——C99/LLVM 后端还没发"构造函数里调 c6c_tls_setup()"那条，
# 跑起来会踩空 %fs 基址。等三后端都支持了就把这个文件删掉。
for f in "$CORPUS"/*.c; do
    name=$(basename "$f" .c)
    if [ -f "$CORPUS/native-only.txt" ] && grep -qx "$name" "$CORPUS/native-only.txt"; then
        continue
    fi
    # 仅编译用例（`/* compile-only */`，通常没有 main）：比不了"标准输出 + 退出码"，
    # 和 llvm-diff.sh 一样如实跳过（run.sh 那边会按 -fsyntax-only 单独验）。
    if grep -q 'compile-only' "$f"; then
        skip=$((skip + 1))
        continue
    fi
    if ! timeout -k 5 "$TMO" "$C6C" "$f" -o "$TMP/$name.n" >"$TMP/$name.nlog" 2>&1; then
        bad=$((bad + 1)); badlist="$badlist $name(native-cc)"; continue
    fi
    if ! timeout -k 5 "$TMO" "$C6C" --emit-c "$f" -o "$TMP/$name.c" >"$TMP/$name.clog" 2>&1; then
        bad=$((bad + 1)); badlist="$badlist $name(c99-emit)"; continue
    fi
    if ! timeout -k 5 "$TMO" "$CC" -w -O1 -o "$TMP/$name.cx" "$TMP/$name.c" >>"$TMP/$name.clog" 2>&1; then
        bad=$((bad + 1)); badlist="$badlist $name(c99-cc)"; continue
    fi
    timeout -k 5 "$RUN_TMO" "$TMP/$name.n" >"$TMP/$name.nout" 2>&1; nrc=$?
    timeout -k 5 "$RUN_TMO" "$TMP/$name.cx" >"$TMP/$name.cout" 2>&1; crc=$?
    if [ "$nrc" -ne "$crc" ]; then
        bad=$((bad + 1)); badlist="$badlist $name(rc $nrc!=$crc)"; continue
    fi
    if ! cmp -s "$TMP/$name.nout" "$TMP/$name.cout"; then
        bad=$((bad + 1)); badlist="$badlist $name(out)"; continue
    fi
    ok=$((ok + 1))
done

echo "c6c: C99 后端 vs native 后端（$CORPUS）：一致 $ok / 不一致 $bad / 跳过 $skip"
if [ "$bad" -ne 0 ]; then
    echo "c6c: 不一致项：$badlist"
    exit 1
fi
