#!/bin/bash
# kernel-vmlinux.sh —— 验证 c6c 编译的 .o 能链接进 Linux vmlinux 并正常启动。
#
# 口径（重要，别读成"c6c 能编整个内核"）：
#   * 本脚本把 Linux 内核里一批"无 inline 符号泄漏"的 .c 用 c6c 重新编译成
#     ET_REL .o，替换掉 gcc 的对应 .o，然后走 kbuild 正常链接出 vmlinux/bzImage。
#   * 判据：qemu 启动后打印 Linux version、初始化控制台与网络协议栈、走到
#     与 gcc 基线相同的 VFS panic（本环境无 rootfs，这是预期终点）。
#   * 这证明 c6c 的 .o 与内核链接器 / 链接脚本 ABI 兼容、链接出的内核功能正常。
#     它不是"999 个内核 TU 全部 c6c 编译"——后者被 c6c 不做内联优化挡着，
#     见下面 KNOWN LIMITS。
#
# 前置：
#   * 内核源码在 $K（默认 /tmp/linux-7.4-rc3），已 make olddefconfig + 至少
#     构建过一次 gcc 基线 vmlinux/bzImage（需 libelf-dev 编 objtool）。
#   * qemu-system-x86_64 + pc-bios rom（默认 /usr/share/qemu 缺 rom 会忽略
#     -kernel 直接走 BIOS；android SDK 那份能用）。
#   * $C6C 是 c6c 可执行文件（默认 ./c6c/build/c6c）。
#
# 用法：
#   K=/path/to/linux C6C=/path/to/c6c QEMU_BIOS=/path/to/pc-bios \
#       bash c6c/tests/kernel-vmlinux.sh
#
# KNOWN LIMITS（为什么不是全部 TU）：
#   c6c 不做内联优化，内核头文件里上万个 static/extern inline 会全量发射成
#     全局符号，`native_save_fl` 等与真实定义 multiple definition。纯数据表
#     TU（ctype/clz_tab）和简单函数 TU（div64/int_pow/int_sqrt/uuid）不触发，
#     可安全替换。完整内核需给 c6c 补内联 / 死代码消除。

set -e
K=${K:-/tmp/linux-7.4-rc3}
C6C=${C6C:-./c6c/build/c6c}
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_BIOS=${QEMU_BIOS:-/persistent/home/winger/sdk/android-sdk/emulator/lib/pc-bios}

SAFE_TUS="lib/ctype.c lib/clz_tab.c lib/math/div64.c lib/math/int_pow.c lib/math/int_sqrt.c lib/uuid.c"
FLAGS='-c -ffreestanding-kernel -nostdinc -DKBUILD_MODNAME="x" -DKBUILD_MODFILE="x" -include include/linux/kconfig.h -include include/generated/autoconf.h -D__KERNEL__ -DCONFIG_X86_64=1 -Iarch/x86/include -Iarch/x86/include/generated -Iinclude -Iarch/x86/include/uapi -Iarch/x86/include/generated/uapi -Iinclude/uapi -Iinclude/generated -Iinclude/generated/uapi'

TAG=$$
BACKUP=/tmp/c6c-vmlinux-backup.$TAG
mkdir -p "$BACKUP"

echo "== kernel: $K"
echo "== c6c:    $C6C"
cd "$K"
cp .config "$BACKUP/.config"

# 1) 备份 gcc .o，逐个用 c6c 重编替换（跳过会泄漏冲突符号的）
REPLACED=0
for f in $SAFE_TUS; do
    o="${f%.c}.o"
    [ -f "$o" ] || { echo "缺少 $o，跳过"; continue; }
    mkdir -p "$BACKUP/$(dirname "$o")"
    cp -f "$o" "$BACKUP/$o"
    ( eval "$C6C $FLAGS $f -o $BACKUP/new.o" ) > /dev/null
    if nm -g --defined-only "$BACKUP/new.o" 2>/dev/null | grep -q ' native_save_fl$'; then
        echo "-- 跳过 $f（泄漏 native_save_fl，会 multiple definition）"
        cp -f "$BACKUP/$o" "$o"
        continue
    fi
    cp -f "$BACKUP/new.o" "$o"
    echo "-- 已替换 $o（c6c）"
    REPLACED=$((REPLACED+1))
done
touch lib/built-in.a lib/math/built-in.a 2>/dev/null || true
echo "== 替换了 $REPLACED 个 TU"

# 2) 只重链接（不换 config，不触发全量重建）
echo "== 链接 vmlinux / bzImage"
make -j8 vmlinux bzImage > "$BACKUP/link.log" 2>&1 || {
    echo "链接失败，最后 20 行："; tail -20 "$BACKUP/link.log"
    cp -f "$BACKUP"/.config .config; exit 1;
}

# 3) qemu 启动验证
echo "== qemu 启动"
timeout 20 "$QEMU" -L "$QEMU_BIOS" -kernel arch/x86/boot/bzImage \
    -m 512M -nographic -no-reboot -append "console=ttyS0 panic=-1" \
    > "$BACKUP/qemu.log" 2>&1 || true
CLEAN=$(sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1bc//g' "$BACKUP/qemu.log")
echo "$CLEAN" | grep -E 'Linux version|Console:|NET: Registered|Kernel panic' | head -10

LINUX=$(echo "$CLEAN" | grep -c 'Linux version')
NET=$(echo "$CLEAN" | grep -c 'NET: Registered')
if [ "$LINUX" -ge 1 ] && [ "$NET" -ge 3 ]; then
    echo "PASS: $REPLACED 个 c6c .o 链接的 vmlinux 正常启动（Linux version + 网络协议栈初始化）"
    RC=0
else
    echo "FAIL: 内核未正常启动（Linux=$LINUX NET=$NET）"
    RC=1
fi

# 4) 恢复 gcc 基线（保证内核树干净）
echo "== 恢复 gcc .o 并重链基线"
for f in $SAFE_TUS; do
    o="${f%.c}.o"
    [ -f "$BACKUP/$o" ] && cp -f "$BACKUP/$o" "$o"
done
touch lib/built-in.a lib/math/built-in.a 2>/dev/null || true
cp "$BACKUP/.config" .config
make -j8 vmlinux bzImage > "$BACKUP/restore.log" 2>&1 || true
echo "== 验证日志留在 $BACKUP（qemu.log / link.log）"

exit $RC
