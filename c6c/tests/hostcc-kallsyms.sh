#!/bin/bash
# hostcc-kallsyms.sh —— 验证 c6c 能编译并链接内核 host tool kallsyms，
# 且输出与 gcc 编译的 kallsyms 逐字节一致（阶段 2：HOSTCC 去 gcc）。
#
# 做法：
#   scripts/kallsyms.c + kallsyms-sysmap.c + elf-parse.c 三 TU 分别用 c6c 编译
#   （c6c 多 TU 一次编译有状态污染，必须逐个 TU），再和 c6c 自带运行时 rt.c6
#   一起用 c6c 的静态链接器链成可执行文件。kallsyms 用不到 environ，
#   -Wl,--allow-undefined 让链接器忽略 rt.c6 里那格零存储。
# 判据：
#   对同一个 ELF（默认内核树的 vmlinux）分别跑 c6c 版和 gcc 版 kallsyms：
#   --sysmap 输出与中间 .bin 必须逐字节一致；生成的 .S 仅 .incbin 文件名不同。
#
# 前置：内核树已构建过 gcc 基线 vmlinux（或有其他 ELF 传第二参数）。
set -u
K=${1:-/tmp/linux-7.4-rc3}
ELF=${2:-$K/vmlinux}
C6C=${C6C:-/media/winger/_dde_data/superc/c6c/build/c6c}
RT=${RT:-/media/winger/_dde_data/superc/c6c/lib/rt.c6}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cd "$K" || exit 1
INC="-I . -I scripts/include -I scripts -I tools/include"

echo "=== 逐个 TU 用 c6c 编译 kallsyms ==="
for src in kallsyms kallsyms-sysmap elf-parse; do
    "$C6C" $INC -c "scripts/$src.c" -o "$TMP/$src.o" >/dev/null 2>&1 || {
        echo "FAIL: 编译 $src.c"; exit 1;
    }
done
"$C6C" -c "$RT" -o "$TMP/rt.o" >/dev/null 2>&1 || { echo "FAIL: 编译 rt.c6"; exit 1; }

echo "=== 用 c6c 静态链接器链接 ==="
"$C6C" -Wl,--allow-undefined "$TMP/kallsyms.o" "$TMP/kallsyms-sysmap.o" \
    "$TMP/elf-parse.o" "$TMP/rt.o" -o "$TMP/kallsyms.c6c" >/dev/null 2>&1 || {
    echo "FAIL: 链接"; exit 1;
}

echo "=== 对 $ELF 跑两版 kallsyms ==="
"$TMP/kallsyms.c6c" --pc-relative --sysmap="$TMP/c6c.map" "$ELF" "$TMP/c6c.bin" \
    > "$TMP/c6c.S" 2>/dev/null || { echo "FAIL: c6c kallsyms 运行"; exit 1; }
scripts/kallsyms --pc-relative --sysmap="$TMP/gcc.map" "$ELF" "$TMP/gcc.bin" \
    > "$TMP/gcc.S" 2>/dev/null || { echo "FAIL: gcc kallsyms 运行"; exit 1; }

echo "=== 逐字节比对 ==="
rc=0
cmp -s "$TMP/c6c.map" "$TMP/gcc.map" && echo "OK  sysmap 逐字节一致" || { echo "FAIL sysmap"; rc=1; }
cmp -s "$TMP/c6c.bin" "$TMP/gcc.bin" && echo "OK  .bin 逐字节一致" || { echo "FAIL .bin"; rc=1; }
# .S 只差 .incbin 的文件名，统一替换后再比。
sed 's#c6c\.bin#K.bin#g' "$TMP/c6c.S" > "$TMP/c6c.norm"
sed 's#gcc\.bin#K.bin#g' "$TMP/gcc.S" > "$TMP/gcc.norm"
cmp -s "$TMP/c6c.norm" "$TMP/gcc.norm" && echo "OK  .S 一致（仅 .incbin 文件名不同）" || { echo "FAIL .S"; rc=1; }

exit $rc
