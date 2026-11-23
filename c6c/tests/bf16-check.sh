#!/bin/sh
# bf16 原生支持的回归：native / C99 / LLVM 三个后端跑同一份断言（tests/bf16_main.c6）。
#
# 钉住的语义：
#   * 对象 2 字节、对齐 2；值是 f32，读出来左移 16 展开、写回去按 RNE 舍入；
#   * NaN 压成 0x7FC0 不变 Inf、最大有限值 ×2 溢出到 Inf；
#   * 局部量 / 形参 / 返回值 / 数组元素 / 结构体成员 / static·全局初始化器；
#   * 比较、算术提升、int↔bf16、bf16↔float/double；
#   * 关键字只属于 .c6：`.c` 里 `bf16` 仍是普通标识符（老代码不断）。
#
# 用法（工作目录 c6c/，和 bcheck.sh 一样）：
#   C6C=build/c6c tests/bf16-check.sh
set -u

C6C=${C6C:-./build/c6c}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
pass=0
fail=0
skip=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }
skp() { skip=$((skip + 1)); echo "SKIP $1（$2）"; }

case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac

SRC=tests/bf16_main.c6

# --- 1. native 后端 ---
if "$C6C" -o "$WORK/native" "$SRC" >"$WORK/c1.log" 2>&1; then
    if out=$("$WORK/native" 2>&1); then
        ok "native：bf16 断言全过"
    else
        bad "native（运行）" "$out"
    fi
else
    bad "native（编译失败）" "$(sed -n '1,3p' "$WORK/c1.log")"
fi

# --- 2. C99 后端 + 宿主 C 编译器 ---
CC=${C99CC:-gcc}
if ! command -v "$CC" >/dev/null 2>&1; then
    skp "c99" "没有 $CC"
elif "$C6C" --emit-c "$SRC" -o "$WORK/bf16.c" >"$WORK/c2.log" 2>&1; then
    if "$CC" -std=c99 -O0 -w "$WORK/bf16.c" -o "$WORK/c99" >"$WORK/c2b.log" 2>&1; then
        if out=$("$WORK/c99" 2>&1); then
            ok "c99：同一份断言全过（$CC 编生成的 .c）"
        else
            bad "c99（运行）" "$out"
        fi
    else
        bad "c99（$CC 编不过生成的 .c）" "$(sed -n '1,3p' "$WORK/c2b.log")"
    fi
else
    bad "c99（--emit-c 失败）" "$(sed -n '1,3p' "$WORK/c2.log")"
fi

# --- 3. LLVM 后端 + clang ---
LLVMCC=${LLVMCC:-clang}
if ! command -v "$LLVMCC" >/dev/null 2>&1; then
    skp "llvm" "没有 $LLVMCC"
elif [ ! -f ../tcc/libtcc1.a ]; then
    skp "llvm" "缺 ../tcc/libtcc1.a"
elif "$C6C" --emit-llvm "$SRC" -o "$WORK/bf16.ll" >"$WORK/c3.log" 2>&1; then
    if "$LLVMCC" -O0 -fuse-ld=lld "$WORK/bf16.ll" ../tcc/libtcc1.a \
        -o "$WORK/llvm" >"$WORK/c3b.log" 2>&1; then
        if out=$("$WORK/llvm" 2>&1); then
            ok "llvm：同一份断言全过（$LLVMCC 编生成的 .ll）"
        else
            bad "llvm（运行）" "$out"
        fi
    else
        bad "llvm（$LLVMCC 编不过生成的 .ll）" "$(sed -n '1,3p' "$WORK/c3b.log")"
    fi
else
    bad "llvm（--emit-llvm 失败）" "$(sed -n '1,3p' "$WORK/c3.log")"
fi

# --- 4. 纯 C 文件里 `bf16` 还是普通标识符 ---
cat >"$WORK/plain.c" <<'EOF'
int bf16 = 1;
int main(void) { return bf16 - 1; }
EOF
if "$C6C" -o "$WORK/plain" "$WORK/plain.c" >"$WORK/c4.log" 2>&1; then
    if "$WORK/plain"; then
        ok ".c 里 bf16 仍是普通标识符（老代码不断）"
    else
        bad ".c 里 bf16 被当成了类型" "退出码 $?"
    fi
else
    bad ".c 里 bf16 不再是普通标识符" "$(sed -n '1,3p' "$WORK/c4.log")"
fi

# --- 5. .c6 里 `bf16` 是类型关键字（负例：当变量名必须报错） ---
cat >"$WORK/bad.c6" <<'EOF'
int bf16 = 1;
int main(void) { return bf16; }
EOF
if "$C6C" -fsyntax-only "$WORK/bad.c6" >"$WORK/c5.log" 2>&1; then
    bad ".c6 里 bf16 没成为类型关键字" "把 bf16 当变量名居然编过"
else
    ok ".c6 里 bf16 是类型关键字"
fi

echo "c6c bf16 回归：通过 $pass / 失败 $fail / 跳过 $skip"
[ "$fail" -eq 0 ]