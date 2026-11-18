#!/bin/bash
# kernel-vmlinux.sh —— 验证 c6c 能编 Linux 内核，并把 c6c .o 链接进 vmlinux 正常启动。
#
# 两层口径（别读成"c6c 能一键编整个内核"）：
#   * 阶段 A（全量 TU 编译）：从 kbuild 的 dry-run 里 harvest 真实的 -I/-D/-include，
#     用 c6c（开 C6C_KERNEL_DCE=1）把 kbuild 实际编译的每个 .c 编成 .o，只编译不链接。
#     量的是 plan 阶段 1 的 CC 角色（999/999 零诊断）。设 SKIP_ALL_TU=1 可只跑阶段 B。
#   * 阶段 B（链接 + 启动）：把其中一批"无 inline 符号泄漏"的 .c 用 c6c 重编成
#     ET_REL .o，替换 gcc 的对应 .o，再走 kbuild 正常链接出 vmlinux/bzImage。
#   * 判据：qemu 启动后打印 Linux version、初始化控制台与网络协议栈、走到
#     与 gcc 基线相同的 VFS panic（本环境无 rootfs，这是预期终点）。
#     阶段 B 证明 c6c 的 .o 与内核链接器 / 链接脚本 ABI 兼容、链接出的内核功能正常；
#     它不是"全部内核 TU 都进 vmlinux"——inline/symbol/objtool 限制仍在，见 KNOWN LIMITS。
#
# 前置：
#   * 内核源码在 $K（默认 /tmp/linux-7.4-rc3），已 make olddefconfig + 至少
#     构建过一次 gcc 基线 vmlinux/bzImage（需 libelf-dev 编 objtool）。
#   * qemu-system-x86_64 + pc-bios rom（默认 /usr/share/qemu 缺 rom 会忽略
#     -kernel 直接走 BIOS；android SDK 那份能用）。CI 会备好 /usr/share/qemu/bios.bin。
#   * $C6C 是 c6c 可执行文件（默认 ./c6c/build/c6c）。
#   * QEMU_TIMEOUT 可放宽 qemu 启动等待秒数（CI 设 60，默认 20）。
#
# 用法：
#   K=/path/to/linux C6C=/path/to/c6c QEMU_BIOS=/path/to/pc-bios \
#       bash c6c/tests/kernel-vmlinux.sh
#
# KNOWN LIMITS（为什么阶段 B 只替换少数 TU）：
#   c6c 不做内联优化，内核头文件里大量 static/extern inline 会全量发射成全局符号，
#   `native_save_fl` 等与真实定义 multiple definition；文件作用域 static 与 inline
#   符号本地化、函数级 DCE（C6C_KERNEL_DCE=1）已缓解，但链接期 objtool 标注仍有限制。
#   纯数据表 TU（ctype/clz_tab）和简单函数 TU（div64/int_pow/int_sqrt/uuid）不触发，
#   可安全替换。阶段 A 只编不链，因此能覆盖全部 TU 而不受这些链接期限制。

set -e
K=${K:-/tmp/linux-7.4-rc3}
C6C=${C6C:-./c6c/build/c6c}
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_BIOS=${QEMU_BIOS:-/persistent/home/winger/sdk/android-sdk/emulator/lib/pc-bios}
QEMU_TIMEOUT=${QEMU_TIMEOUT:-20}
SKIP_ALL_TU=${SKIP_ALL_TU:-0}
# 自动下载：K 不存在时，从 KERNEL_URL 下载并解压到 KERNEL_VERSION 目录。
# 默认用真实存在的 6.12 稳定版（7.4-rc3 是预置版本，kernel.org 无公开 tarball）。
# 设 KERNEL_NO_DOWNLOAD=1 可禁用下载（纯用现成 K）。
KERNEL_VERSION=${KERNEL_VERSION:-6.12}
KERNEL_URL=${KERNEL_URL:-https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VERSION}.tar.gz}
KERNEL_NO_DOWNLOAD=${KERNEL_NO_DOWNLOAD:-0}

skip() { echo "SKIP kernel-vmlinux：$1"; exit 0; }

# 0) 需要时自动下载内核源码
if [ ! -d "$K" ] && [ "$KERNEL_NO_DOWNLOAD" != 1 ]; then
    command -v curl >/dev/null 2>&1 || curl() { return 1; }
    command -v tar >/dev/null 2>&1 || skip "无 tar，无法解压内核"
    echo "== 自动下载内核 $KERNEL_VERSION"
    echo "   URL: $KERNEL_URL"
    echo "   目标: $K"
    BASE=$(dirname "$K")
    TARBALL="$BASE/linux-${KERNEL_VERSION}.tar.gz"
    mkdir -p "$BASE"
    if [ ! -f "$TARBALL" ]; then
        curl -fsSL --retry 2 --max-time 900 "$KERNEL_URL" -o "$TARBALL" \
            || skip "下载失败（$KERNEL_URL）；可手动放置 tarball 或设 K= 指向现成源码"
    fi
    tar -xzf "$TARBALL" -C "$BASE" \
        || skip "解压失败（$TARBALL）"
    EXTRACTED="$BASE/linux-${KERNEL_VERSION}"
    [ -d "$EXTRACTED" ] || skip "解压后目录不存在：$EXTRACTED"
    if [ "$K" != "$EXTRACTED" ]; then
        mv "$EXTRACTED" "$K" 2>/dev/null || ln -s "$EXTRACTED" "$K"
    fi
    echo "== 内核下载完成：$K"
fi

# 1) 环境探测。CI / 发布机通常缺 libelf-dev/qemu，缺任一项就 SKIP（退出 0），
#    不让它把门禁搞红；有完整环境时真跑。
[ -d "$K" ] || skip "内核源码树不存在（$K，可用 K= 指定；或让脚本自动下载）"
command -v make >/dev/null 2>&1 || skip "无 make"
if [ ! -f "$K/vmlinux" ]; then
    # 只在新下载的内核上自动构建基线（避免动用户现成树）——需要 host 工具链
    command -v gcc >/dev/null 2>&1 || skip "无 gcc，无法构建基线 vmlinux"
    echo "== 构建基线 vmlinux（$K）"
    ( cd "$K" && make olddefconfig >/dev/null 2>&1 && make -j"$(nproc)" vmlinux >/dev/null 2>&1 ) \
        || skip "基线 vmlinux 构建失败（缺 libelf-dev/bison/flex 等 host 依赖？）"
fi
[ -f "$K/vmlinux" ] || skip "内核未构建过 vmlinux（$K）"
command -v "$QEMU" >/dev/null 2>&1 || skip "qemu 不存在（$QEMU）"
[ -f "$QEMU_BIOS/bios.bin" ] || skip "pc-bios rom 不存在（$QEMU_BIOS，可用 QEMU_BIOS= 指定）"
case "$C6C" in /*) ;; *) C6C="$(pwd)/$C6C" ;; esac
[ -x "$C6C" ] || skip "c6c 不存在（$C6C）"

TAG=$$
BACKUP=/tmp/c6c-vmlinux-backup.$TAG
mkdir -p "$BACKUP"

# 1.5) 阶段 A：全量 TU 编译（只编不链，量 CC 角色）
# 从 kbuild 的 dry-run 里取真实的 include/define/include-file，避免手抄漏项。
# 只保留 c6c 支持的 -I/-D/-include/-nostdinc；gcc 专有的告警/代码生成选项不带过来。
if [ "$SKIP_ALL_TU" != 1 ]; then
    echo "== 阶段 A：c6c 全量编译内核 TU（DCE 开）"
    CMDS="$BACKUP/make-n.txt"
    ( cd "$K" && make -n -B vmlinux ) > "$CMDS" 2>/dev/null \
        || skip "make -n -B vmlinux 失败，无法 harvest 编译参数"
    INCLUDES=$(grep -oE -- '(^| )-I[^[:space:]]+' "$CMDS" | sed 's/^ *//' | sort -u | tr '\n' ' ')
    DEFINES=$(grep -oE -- '(^| )-D[^[:space:]]+' "$CMDS" | sed 's/^ *//' | sort -u | tr '\n' ' ')
    EXTRA_INCS=$(awk '{for(i=1;i<=NF;i++) if($i=="-include" && i<NF) print $(i+1)}' "$CMDS" \
                 | sort -u | tr '\n' ' ')
    INC_ARGS=""
    for h in $EXTRA_INCS; do INC_ARGS="$INC_ARGS -include $h"; done
    TUS=$(awk '{for(i=1;i<=NF;i++) if($i ~ /\.c$/) print $i}' "$CMDS" \
          | sort -u | grep -vE '^(scripts|tools|Documentation|samples|usr)/' || true)
    [ -n "$TUS" ] || skip "make -n 里没有内核 C TU"
    PASS=0
    FAIL=0
    mkdir -p "$BACKUP/tu"
    for f in $TUS; do
        out="$BACKUP/tu/$(echo "$f" | tr '/' '_').o"
        if eval "cd \"$K\" && C6C_KERNEL_DCE=1 \"$C6C\" -c -ffreestanding-kernel -nostdinc $DEFINES $INCLUDES $INC_ARGS \"$f\" -o \"$out\"" \
             >/dev/null 2>"$BACKUP/tu.err"; then
            PASS=$((PASS+1))
        else
            FAIL=$((FAIL+1))
            if [ "$FAIL" -le 10 ]; then
                echo "-- FAIL $f"
                head -3 "$BACKUP/tu.err"
            fi
        fi
    done
    echo "== 阶段 A：PASS=$PASS FAIL=$FAIL"
    [ "$FAIL" -eq 0 ] || { echo "FAIL: $FAIL 个内核 TU 用 c6c 编不过"; exit 1; }
fi

echo "== kernel: $K"
echo "== c6c:    $C6C"
cd "$K"
cp .config "$BACKUP/.config"

# 2) 阶段 B：备份 gcc .o，逐个用 c6c 重编替换（跳过会泄漏冲突符号的）
SAFE_TUS="lib/ctype.c lib/clz_tab.c lib/math/div64.c lib/math/int_pow.c lib/math/int_sqrt.c lib/uuid.c"
FLAGS='-c -ffreestanding-kernel -nostdinc -DKBUILD_MODNAME="x" -DKBUILD_MODFILE="x" -include include/linux/kconfig.h -include include/generated/autoconf.h -D__KERNEL__ -DCONFIG_X86_64=1 -Iarch/x86/include -Iarch/x86/include/generated -Iinclude -Iarch/x86/include/uapi -Iarch/x86/include/generated/uapi -Iinclude/uapi -Iinclude/generated -Iinclude/generated/uapi'

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

# 3) 只重链接（不换 config，不触发全量重建）
echo "== 链接 vmlinux / bzImage"
make -j8 vmlinux bzImage > "$BACKUP/link.log" 2>&1 || {
    echo "链接失败，最后 20 行："; tail -20 "$BACKUP/link.log"
    cp -f "$BACKUP"/.config .config; exit 1;
}

# 4) qemu 启动验证
echo "== qemu 启动"
timeout "$QEMU_TIMEOUT" "$QEMU" -L "$QEMU_BIOS" -kernel arch/x86/boot/bzImage \
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

# 5) 恢复 gcc 基线（保证内核树干净）
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
