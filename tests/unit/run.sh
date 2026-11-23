#!/bin/sh
# c6c 单元测试统一入口（check-all / fast 都走这一条）。
#
# 用法（工作目录随意，脚本自己按 ROOT 定位）：
#   C6C=c6c/build/c6c ROOT=. tests/unit/run.sh
#
# 覆盖三块：
#   1. 编译器内置自测：`--type-selftest`（类型表）与 `--ir-selftest`（IR 编码）；
#   2. IR dump 稳定：两次 `--ir-demo` 必须逐字节相同（IR 是契约）；
#   3. `tests/unit/cases/*.c6`：每份是一个自断言程序，退出码 0 = 通过。
#      同名 `.meta` 里写 `backends=native c99 llvm` 时三个后端各跑一遍
#      （c99 要宿主 cc、llvm 要 clang + tcc/libtcc1.a，缺了就如实 SKIP）。
#
# 加一个单元测试：把自断言的 `.c6` 放进 cases/ 就行，不需要改 CMake 和 CI。
set -u

C6C=${C6C:-./c6c/build/c6c}
ROOT=${ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
CASE_DIR=$ROOT/tests/unit/cases
CC=${C99CC:-gcc}
LLVMCC=${LLVMCC:-clang}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
skip=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }
skp() { skip=$((skip + 1)); echo "SKIP $1（$2）"; }

# --- 1. 内置自测 ---------------------------------------------------------
if out=$("$C6C" --type-selftest 2>&1); then
    ok "内置自测：类型表"
else
    bad "内置自测：类型表" "$(printf '%s' "$out" | tail -2)"
fi
if out=$("$C6C" --ir-selftest 2>&1); then
    ok "内置自测：IR 编码"
else
    bad "内置自测：IR 编码" "$(printf '%s' "$out" | tail -2)"
fi

# --- 2. IR dump 稳定 -----------------------------------------------------
if "$C6C" --ir-demo >"$WORK/ir1.txt" 2>&1 && "$C6C" --ir-demo >"$WORK/ir2.txt" 2>&1; then
    if cmp -s "$WORK/ir1.txt" "$WORK/ir2.txt"; then
        ok "IR dump 稳定（两次 --ir-demo 逐字节相同）"
    else
        bad "IR dump 不稳定" "两次输出不一致"
    fi
else
    bad "IR dump" "--ir-demo 跑不起来"
fi

# --- 3. cases/*.c6 -------------------------------------------------------
run_case_backend() { # $1=case $2=backend $3=可执行文件
    if out=$("$3" 2>&1); then
        ok "$(basename "$1" .c6)（$2）"
    else
        bad "$(basename "$1" .c6)（$2 运行）" "$(printf '%s' "$out" | tail -3)"
    fi
}

for case in "$CASE_DIR"/*.c6; do
    [ -f "$case" ] || continue
    name=$(basename "$case" .c6)
    backends="native"
    if [ -f "$CASE_DIR/$name.meta" ]; then
        backends=$(sed -n 's/^backends=//p' "$CASE_DIR/$name.meta" | head -1)
        [ -n "$backends" ] || backends="native"
    fi

    for be in $backends; do
        case "$be" in
        native)
            if "$C6C" -o "$WORK/$name.native" "$case" >"$WORK/$name.native.log" 2>&1; then
                run_case_backend "$case" native "$WORK/$name.native"
            else
                bad "$name（native 编译）" "$(sed -n '1,3p' "$WORK/$name.native.log")"
            fi
            ;;
        c99)
            if ! command -v "$CC" >/dev/null 2>&1; then
                skp "$name（c99）" "没有 $CC"
            elif ! "$C6C" --emit-c "$case" -o "$WORK/$name.c" >"$WORK/$name.c.log" 2>&1; then
                bad "$name（c99 --emit-c）" "$(sed -n '1,3p' "$WORK/$name.c.log")"
            elif ! "$CC" -std=c99 -O0 -w "$WORK/$name.c" -o "$WORK/$name.c99" \
                    >"$WORK/$name.c99.log" 2>&1; then
                bad "$name（c99 编不过）" "$(sed -n '1,3p' "$WORK/$name.c99.log")"
            else
                run_case_backend "$case" c99 "$WORK/$name.c99"
            fi
            ;;
        llvm)
            if ! command -v "$LLVMCC" >/dev/null 2>&1; then
                skp "$name（llvm）" "没有 $LLVMCC"
            elif [ ! -f "$ROOT/tcc/libtcc1.a" ]; then
                skp "$name（llvm）" "缺 tcc/libtcc1.a"
            elif ! "$C6C" --emit-llvm "$case" -o "$WORK/$name.ll" >"$WORK/$name.ll.log" 2>&1; then
                bad "$name（llvm --emit-llvm）" "$(sed -n '1,3p' "$WORK/$name.ll.log")"
            elif ! "$LLVMCC" -O0 -fuse-ld=lld "$WORK/$name.ll" "$ROOT/tcc/libtcc1.a" \
                    -o "$WORK/$name.llvm" >"$WORK/$name.llvm.log" 2>&1; then
                bad "$name（llvm 编不过）" "$(sed -n '1,3p' "$WORK/$name.llvm.log")"
            else
                run_case_backend "$case" llvm "$WORK/$name.llvm"
            fi
            ;;
        *)
            skp "$name（$be）" "不认识的 backend"
            ;;
        esac
    done
done

echo "c6c 单元测试：通过 $pass / 失败 $fail / 跳过 $skip"
[ "$fail" -eq 0 ]
