#!/bin/sh
# 备齐 `build-tcc` 要的 tcc 语料依赖（三个**生成**物，都不是 tcc 的产品构建）：
#   config.h / config.mak —— tcc/CMakeLists.txt 生成（只跑 tcc-config target）
#   tccdefs_.h            —— tcc/Makefile 的规则（宿主 cc 出 c2str 再转换）
#   libtcc1.a             —— tcc 的 C 运行库，必须由**能用的 tcc** 编
# 已经备好时三步都是空操作。从 c6c/Makefile 的 build-tcc 依赖搬过来的。
set -eu

ROOT=${ROOT:?用法: ROOT=/path/to/superc BUILD=c6c/build tests/tcc-corpus.sh}
BUILD=${BUILD:?需要 BUILD=c6c/build}
CMAKE=${CMAKE:-cmake}
MAKE=${MAKE:-make}
C6EXT=${C6EXT:--DC6_EXT}
STAMP="$BUILD/tcc-config.stamp"

mkdir -p "$BUILD"

if [ ! -f "$ROOT/tcc/config.h" ]; then
    echo "c6c: 生成 tcc 的配置头（只跑 tcc-config，不编 tcc）"
    "$CMAKE" -S "$ROOT/tcc" -B "$BUILD/tcc-config" > /dev/null
    "$CMAKE" --build "$BUILD/tcc-config" --target tcc-config > /dev/null
fi

if [ ! -f "$ROOT/tcc/tccdefs_.h" ]; then
    echo "c6c: 生成 tcc/tccdefs_.h（tcc/Makefile 的规则，用宿主 cc）"
    "$MAKE" -C "$ROOT/tcc" tccdefs_.h "EXTRA-DEFS=$C6EXT" > /dev/null
fi

if [ ! -f "$ROOT/tcc/libtcc1.a" ]; then
    if [ ! -x "$ROOT/tcc/c6" ]; then
        echo "c6c: 用宿主 cc 编一个参考 tcc（G2c 的运行时夹具，不安装）"
        "$MAKE" -C "$ROOT/tcc" c6 "EXTRA-DEFS=$C6EXT" > /dev/null
    fi
    echo "c6c: 用参考 tcc 编 tcc 的 C 运行库 libtcc1.a"
    "$MAKE" -C "$ROOT/tcc/lib" "EXTRA-DEFS=$C6EXT" > /dev/null
fi

touch "$STAMP"
