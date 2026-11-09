#!/bin/sh
# 构建「裸机」镜像并启动：BIOS 引导扇区 + 我们自己写的引导器 + c6 程序。
# 没有内核、没有 libc、没有 syscall。用法：
#
#   examples/boot/build.sh            # 有 KVM 就用 KVM
#   NO_KVM=1 examples/boot/build.sh   # 强制用 TCG（纯软件模拟）
#
# 产物：
#   build/payload.elf / .bin   c6 程序（tcc 链接在 0x400000）
#   build/boot.bin             512 字节引导扇区
#   build/bare.img             引导扇区 + payload，qemu 当硬盘启动
#
# qemu 的退出码来自 isa-debug-exit：(code << 1) | 1，所以 code=0 时是 1。
set -e
export LC_ALL=C

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
build=build
mkdir -p "$build"

echo "== 1. 编译 payload（c6 程序 + libtcc1）"
./tcc/tcc -B tcc -std=c6 -static -nostdlib -o "$build/payload.elf" \
    examples/boot/main.c tcc/libtcc1.a
info=$(python3 examples/boot/flat.py "$build/payload.elf" "$build/payload.bin" 0x400000)
# shellcheck disable=SC2086
set -- $info
entry=$1
bytes=$2
memsz=$3
bss=$((memsz - bytes))
sectors=$(( (bytes + 511) / 512 ))
echo "   payload: $bytes 字节文件 + $bss 字节 .bss（$sectors 个扇区），入口 $entry"

echo "== 2. 汇编引导扇区"
as --32 -o "$build/boot.o" \
    --defsym payload_sectors="$sectors" \
    --defsym payload_bytes="$bytes" \
    --defsym payload_bss="$bss" \
    --defsym payload_entry="$entry" \
    examples/boot/boot.S
ld -m elf_i386 -T examples/boot/boot.ld -o "$build/boot.elf" "$build/boot.o"
objcopy -O binary "$build/boot.elf" "$build/boot.bin"
sz=$(stat -c %s "$build/boot.bin")
if [ "$sz" -gt 512 ]; then
    echo "引导扇区超了 512 字节（$sz），要拆成两级" >&2
    exit 1
fi
echo "   引导扇区: $sz 字节"

echo "== 3. 拼镜像"
cp "$build/boot.bin" "$build/bare.img"
dd if=/dev/zero of="$build/bare.img" bs=1 seek="$sz" count=$((512 - sz)) \
   conv=notrunc 2>/dev/null || true
cat "$build/payload.bin" >> "$build/bare.img"

echo "== 4. 启动 qemu-system-x86_64"
kvm=""
if [ -z "$NO_KVM" ] && [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    kvm="-enable-kvm"
fi
set +e
qemu-system-x86_64 $kvm \
    -drive "file=$build/bare.img,format=raw,if=ide" -boot c -m 64 \
    -display none -debugcon stdio -device isa-debug-exit -no-reboot
rc=$?
set -e

if [ $rc -eq 1 ]; then
    echo "fstest-bare: ok（裸机启动，全部断言通过；$([ -n "$kvm" ] && echo KVM || echo TCG)）"
else
    echo "fstest-bare: FAILED（qemu 退出码 $rc；1 才是通过）" >&2
    exit 1
fi
