#!/bin/sh
# 构建「裸机」镜像并启动：BIOS 引导扇区 + 我们自己写的引导器 + c6 程序。
# 没有内核、没有 libc、没有 syscall。用法：
#
#   examples/boot/build.sh                    # 跑 examples/boot/main.c6
#   examples/boot/build.sh 源文件...          # 换个 payload（fstest-bare-tests 这么做）
#                                             # （语言看后缀：.c6 是 c6，.c/.plain.c 是普通 C）
#   NO_KVM=1 examples/boot/build.sh           # 强制用 TCG（纯软件模拟）
#   SMP=2 examples/boot/build.sh 源文件...    # 多核（-smp 2，并 -DC6_SMP_CPUS=2）
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

payload_src="examples/boot/main.c6 examples/boot/io.c"
if [ $# -gt 0 ]; then
    payload_src="$*"
fi

echo "== 1b. 汇编 AP 跳板（多核用，as --32）"
as --32 -o "$build/ap.o" examples/boot/ap.S
ld -m elf_i386 -Ttext 0x8000 --oformat binary -o "$build/ap.bin" "$build/ap.o"
# 包成 ELF 目标文件：符号是 _binary_ap_bin_{start,end}，sched.plain.c 里引用
(cd "$build" && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    ap.bin ap_blob.o)
APOBJ="$build/ap_blob.o"
echo "   AP 跳板: $(stat -c %s "$build/ap.bin") 字节（加载到 0x8000）"

echo "== 1. 编译 payload（c6 程序 + libtcc1）"
# 每个源文件单独编成 .o 再链接：一次性把多个 TU 交给 tcc 会走到编译器的一个
# 老 bug（c6_gc_new_lead 里对 NULL 标签名 strcmp，带 new/GC 的 TU 撞上就崩），
# 拆成 -c + 链接就绕开了。
objs=""
n=0
if [ -n "$ONE_TU" ]; then
    # 有的测试把 test 块分散在 tu 文件里，而 -test 的编号是整次编译连续的，
    # 这种只能一口气编（见 runtests.sh）。.plain.c 仍旧单独编成 .o 混进去。
    c6src=""
    plainobj=""
    for src in $payload_src; do
        case "$src" in
        *.plain.c)
            obj="$build/payload_$n.o"
            n=$((n + 1))
            ./tcc/tcc -B tcc $EXTRA_CFLAGS -c -o "$obj" "$src"
            plainobj="$plainobj $obj"
            ;;
        *) c6src="$c6src $src" ;;
        esac
    done
    ./tcc/tcc -B tcc -static -nostdlib -o "$build/payload.elf" \
        $c6src $plainobj tcc/libtcc1.a "$APOBJ"
    n=-1
fi
for src in $payload_src; do
    [ "$n" -lt 0 ] && break
    obj="$build/payload_$n.o"
    n=$((n + 1))
    # 语言由后缀决定：.c6 自己开启扩展，.c / .plain.c 是普通 C（要 .bss，
    # 也不受 c6 的"每个变量都要初始化器"的规矩约束），所以这里没有开关。
    ./tcc/tcc -B tcc $EXTRA_CFLAGS -c -o "$obj" "$src"
    objs="$objs $obj"
done
[ "$n" -lt 0 ] || \
    ./tcc/tcc -B tcc -static -nostdlib -o "$build/payload.elf" $objs \
        tcc/libtcc1.a "$APOBJ"
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
qemu-system-x86_64 $kvm -smp "${SMP:-1}" \
    -drive "file=$build/bare.img,format=raw,if=ide" -boot c -m 64 \
    -display none -debugcon stdio -device isa-debug-exit -no-reboot
rc=$?
set -e

if [ $rc -eq 1 ]; then
    echo "fstest-bare: ok（裸机启动，全部断言通过；$([ -n "$kvm" ] && echo KVM || echo TCG)；$([ "${SMP:-1}" -gt 1 ] && echo "${SMP:-1} 核" || echo 单核)）"
else
    echo "fstest-bare: FAILED（qemu 退出码 $rc；1 才是通过）" >&2
    exit 1
fi
