#!/bin/sh
# 自举 fixpoint（验收第 1 条的核心）：c6c 编自己 → c6c2，c6c2 再编自己 → c6c3，
# 两份二进制必须逐字节相同。从 c6c/Makefile 的 selfhost 目标搬过来的。
set -u
C6C=${C6C:?用法: C6C=build/c6c BUILD=build SRC=src/c6c.c6 C6C_RT=lib/rt.c6 INC=include tests/selfhost.sh}
BUILD=${BUILD:?需要 BUILD=输出目录}
SRC=${SRC:?需要 SRC=src/c6c.c6}
C6C_RT=${C6C_RT:?需要 C6C_RT=lib/rt.c6}
INC=${INC:?需要 INC=include}

"$C6C" -o "$BUILD/c6c2" "$SRC" || exit 1
C6C_RT="$C6C_RT" "$BUILD/c6c2" -I"$INC" -o "$BUILD/c6c3" "$SRC" || exit 1
cmp "$BUILD/c6c2" "$BUILD/c6c3" || {
    echo "c6c: 自举 fixpoint 不一致（c6c2 != c6c3）"
    exit 1
}
echo "c6c: 自举 fixpoint 逐字节相同（c6c2 == c6c3，$(wc -c < "$BUILD/c6c2") 字节）"
