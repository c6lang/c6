#!/bin/bash
# hostcc-tools.sh —— 验证内核 host tools 能用 c6c 编译链接，且输出与 gcc 版一致
# （阶段 2：HOSTCC 去 gcc）。当前覆盖 kallsyms、fixdep、sorttable 三个。
#
# 做法：逐个 TU 用 c6c 编译（c6c 多 TU 一次编译有状态污染），和 c6c 自带运行时
# rt.c6 一起用 c6c 静态链接器链接。host tools 用不到 environ，
# -Wl,--allow-undefined 让链接器忽略 rt.c6 里那格零存储。
#
# 判据（每项都与 gcc 版对比）：
#   kallsyms : --sysmap 输出与中间 .bin 逐字节一致；.S 仅 .incbin 文件名不同
#   fixdep   : 对真实 .d 文件的 stdout 与 depfile 修改逐字节一致
#   sorttable: 对同一 vmlinux 排序后的 ELF 逐字节一致
#
# 前置：内核树已构建过 gcc 基线 vmlinux / host tools。
set -u
K=${1:-/tmp/linux-7.4-rc3}
ELF=${2:-$K/vmlinux}
C6C=${C6C:-/media/winger/_dde_data/superc/c6c/build/c6c}
RT=${RT:-/media/winger/_dde_data/superc/c6c/lib/rt.c6}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cd "$K" || exit 1
INC="-I . -I scripts/include -I scripts -I tools/include"
C6C_LINK="$C6C -Wl,--allow-undefined"
rc=0
ok()   { echo "  OK   $1"; }
fail() { echo "  FAIL $1"; rc=1; }

# 编译运行时（三个工具共用）
"$C6C" -c "$RT" -o "$TMP/rt.o" >/dev/null 2>&1 || { echo "FAIL: 编译 rt.c6"; exit 1; }

echo "=== kallsyms ==="
for src in kallsyms kallsyms-sysmap elf-parse; do
    "$C6C" $INC -c "scripts/$src.c" -o "$TMP/$src.o" >/dev/null 2>&1 || fail "编译 $src.c"
done
$C6C_LINK "$TMP/kallsyms.o" "$TMP/kallsyms-sysmap.o" "$TMP/elf-parse.o" \
    "$TMP/rt.o" -o "$TMP/kallsyms.c6c" >/dev/null 2>&1 || fail "链接"
[ -x "$TMP/kallsyms.c6c" ] && {
    "$TMP/kallsyms.c6c" --pc-relative --sysmap="$TMP/c6c.map" "$ELF" "$TMP/c6c.bin" \
        > "$TMP/c6c.S" 2>/dev/null || fail "c6c 运行"
    scripts/kallsyms --pc-relative --sysmap="$TMP/gcc.map" "$ELF" "$TMP/gcc.bin" \
        > "$TMP/gcc.S" 2>/dev/null || fail "gcc 运行"
    cmp -s "$TMP/c6c.map" "$TMP/gcc.map" && ok "sysmap 一致" || fail "sysmap"
    cmp -s "$TMP/c6c.bin" "$TMP/gcc.bin" && ok ".bin 一致" || fail ".bin"
    sed 's#c6c\.bin#K.bin#g' "$TMP/c6c.S" > "$TMP/c6c.norm"
    sed 's#gcc\.bin#K.bin#g' "$TMP/gcc.S" > "$TMP/gcc.norm"
    cmp -s "$TMP/c6c.norm" "$TMP/gcc.norm" && ok ".S 一致（仅 .incbin 名）" || fail ".S"
}

echo "=== fixdep ==="
"$C6C" -I scripts/include -c scripts/basic/fixdep.c -o "$TMP/fixdep.o" >/dev/null 2>&1 || fail "编译"
$C6C_LINK "$TMP/fixdep.o" "$TMP/rt.o" -o "$TMP/fixdep.c6c" >/dev/null 2>&1 || fail "链接"
[ -x "$TMP/fixdep.c6c" ] && {
    SRC="$K/init/main.c"
    printf '/tmp/o.o: %s %s/include/linux/kconfig.h\n' "$SRC" "$K" > "$TMP/base.d"
    T=/tmp/o.o; CMD="gcc -Wp,-MMD,$TMP/base.d -c -o /tmp/o.o init/main.c"
    cp "$TMP/base.d" "$TMP/g.d"; scripts/basic/fixdep "$TMP/g.d" "$T" "$CMD" \
        > "$TMP/g.out" 2>/dev/null || fail "gcc 运行"
    cp "$TMP/base.d" "$TMP/c.d"; "$TMP/fixdep.c6c" "$TMP/c.d" "$T" "$CMD" \
        > "$TMP/c.out" 2>/dev/null || fail "c6c 运行"
    cmp -s "$TMP/g.out" "$TMP/c.out" && ok "stdout 一致" || fail "stdout"
    cmp -s "$TMP/g.d" "$TMP/c.d" && ok "depfile 一致" || fail "depfile"
}

echo "=== sorttable ==="
"$C6C" $INC -c scripts/sorttable.c -o "$TMP/sorttable.o" >/dev/null 2>&1 || fail "编译"
$C6C_LINK "$TMP/sorttable.o" "$TMP/elf-parse.o" "$TMP/rt.o" \
    -o "$TMP/sorttable.c6c" >/dev/null 2>&1 || fail "链接"
[ -x "$TMP/sorttable.c6c" ] && {
    cp "$ELF" "$TMP/g.elf"; cp "$ELF" "$TMP/c.elf"
    scripts/sorttable "$TMP/g.elf" >/dev/null 2>&1 || fail "gcc 运行"
    "$TMP/sorttable.c6c" "$TMP/c.elf" >/dev/null 2>&1 || fail "c6c 运行"
    cmp -s "$TMP/g.elf" "$TMP/c.elf" && ok "ELF 一致" || fail "ELF"
}

echo "=== recordmcount（本配置 CONFIG_HAVE_FENTRY=y 不启用，仅验证可编译链接） ==="
"$C6C" $INC -c scripts/recordmcount.c -o "$TMP/rm.o" >/dev/null 2>&1 || fail "编译"
$C6C_LINK "$TMP/rm.o" "$TMP/rt.o" -o "$TMP/rm.c6c" >/dev/null 2>&1 || fail "链接"
[ -x "$TMP/rm.c6c" ] && ok "编译链接通过"

echo "=== 结果 ==="
[ $rc -eq 0 ] && echo "全部通过" || echo "有失败项"
exit $rc
