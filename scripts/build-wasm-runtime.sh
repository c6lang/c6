#!/bin/sh
# 为 wasm 目标构建 tcc 运行库：tcc/libtcc1-<arch>.a
#
#   ./scripts/build-wasm-runtime.sh [triple]
#
# triple 默认 wasm32-unknown-unknown：wasm64 的 ABI 与 IR 的 LP64 模型天然
# 一致，但 LLVM 19 的 wasm-ld 给函数指针表发的 elem 段是坏的（见
# docs/llvm-backend.md），现阶段的 wasm 目标用 wasm32。
#
# 用 clang 交叉编 tcc/lib 下的 C 源。跳过三个文件：
#   atomic.S / alloca.S   x86 汇编，wasm 没有
#   tcov.c                x86 覆盖钩子
# 运行期需要的 libc/线程符号由 -nostdlib 链接时的桩提供（见 wasm-rt.c）。
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TRIPLE=${1:-wasm32-unknown-unknown}
ARCH=${TRIPLE%%-*}
CC=${LLVMCC:-clang}
command -v "$CC" >/dev/null 2>&1 || CC=/usr/lib/llvm-19/bin/$CC
AR=${LLVMAR:-llvm-ar}
command -v "$AR" >/dev/null 2>&1 || AR=ar
OUT=$ROOT/tcc/libtcc1-$ARCH.a
TMP=$ROOT/build/wasmrt-$ARCH
SRC=$ROOT/tcc/lib

rm -rf "$TMP" && mkdir -p "$TMP"
n=0
for f in libtcc1.c stdatomic.c builtin.c va_list.c dsohandle.c wasm-rt.c; do
    [ -f "$SRC/$f" ] || continue
    INC=""; DEFS=""
    case $f in
    libtcc1.c)
        INC="-I$ROOT/tcc/include -I$SRC"
        # wasm32 是 ILP32：这些帮助函数带 unsigned long / size_t / 指针参数，
        # 编成 32 位版本并改名，64 位签名的包装在 wasm-rt.c 里
        if [ "$ARCH" = wasm32 ]; then DEFS="-D__c6_gc_new=__c6_gc_new_lp64 -D__c6_gc_root=__c6_gc_root_lp64 -D__c6_fmt_l=__c6_fmt_l_lp64 -D__c6_fmt_s=__c6_fmt_s_lp64 -D__tcc_slice_index=__tcc_slice_index_lp64 -D__tcc_slice_sub=__tcc_slice_sub_lp64 -D__tcc_err_flag_peek=__tcc_err_flag_peek_lp64 -D__tcc_err_set_copy=__tcc_err_set_copy_lp64 -D__tcc_iface_init=__tcc_iface_init_lp64 -D__tcc_sp_args=__tcc_sp_args_lp64"; fi ;;
    wasm-rt.c)
        # 这一份只用 clang 自己的头：tcc/include 的 stddef.h 是给 x86 目标写的
        INC=""
        DEFS="" ;;
    *)
        INC="-I$ROOT/tcc/include -I$SRC" ;;
    esac
    "$CC" --target=$TRIPLE -O1 -c "$SRC/$f" -o "$TMP/${f%.c}.o" $INC $DEFS || {
        echo "wasmrt: $f 编译失败（triple=$TRIPLE）" >&2; exit 1; }
    n=$((n + 1))
done
rm -f "$OUT"
"$AR" rcs "$OUT" "$TMP"/*.o
echo "wasmrt: $OUT（$n 个目标文件，triple=$TRIPLE）"
