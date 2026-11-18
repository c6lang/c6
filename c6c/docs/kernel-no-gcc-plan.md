# 完全用 c6c 替代 gcc 构建 Linux 内核：分阶段路线

> **状态**：阶段 1（CC）已验证完成；阶段 2（HOSTCC kbuild 全链 + modpost）已打通；阶段 4 的 objcopy/ar 子集已完成；阶段 3（AS）与 LD 未开始 · **索引**：[文档索引](../../docs/README.md) · **关联**：[design.md](design.md) §15/§16、[CHANGELOG](../../CHANGELOG.md)

## 目标

完全用 c6c 替代 gcc/binutils 编译 Linux 内核，产出**可启动的 vmlinux**——消除
对 gcc（CC）、as（汇编器）、host tools 的 gcc 编译、GNU ld 链接的依赖。

## gcc 在内核构建里的 5 个角色（实测完整构建日志）

| 角色 | 调用数 | 工具 | c6c 现状 |
|---|---|---|---|
| CC | 3006 | `gcc -c` 编内核 .c | **999/999 TU 两遍发射编译成功，全量链接 + qemu 启动通过**（阶段 1 完成） |
| AS | 75 | `as` 编 `.S` | 无独立汇编器；需 GNU as 兼容前端或改写 75 个 .S（阶段 3） |
| HOSTCC | 6 | gcc 编 kallsyms/modpost/sorttable/fixdep/relocs | **4 个已用 c6c 编译链接**（kallsyms/fixdep/sorttable 输出与 gcc 版逐字节一致，recordmcount 可编链）；modpost/relocs/objtool 未做（阶段 2 进行中） |
| LD | 10 | `ld` 按 vmlinux.lds.S 链接 | 仍用 GNU ld；ld.c6 不支持自定义段布局/归档/kallsyms 两遍（阶段 4） |
| AR/OBJCOPY | 800+ | `ar`/`objcopy` | 仍用 binutils；无 ELF 归档与段抽取工具（阶段 4） |

## 阶段 1：CC 全量替换——已完成

- **两遍发射（`C6C_KERNEL_DCE=2`）**：解析期缓存全部函数 IR 与跨函数引用图，从全局
  根（非 static 函数、ctor/dtor、数据引用、alias 目标）做可达性分析，第二遍只发射
  可达函数。从根本上避免死代码字节与膨胀——不能物理后压缩（`x64_branch` 把 rel32
  目标拍死在代码字节里、无重定位，搬移必错）。
- **配套修复（均已提交并推送）**：
  - inline 符号本地化 + `PA_INLINE_SEEN` 每条顶层声明重置；
  - 精确 alias 根（`ALIAS_FROM`/`ALIAS_TO`），避免 2396 个方法别名误当根导致膨胀；
  - syscall wrapper 不被 inline 本地化（全量链接关键）；
  - weak+alias 纯声明发 WEAK；top-level asm 的 `.long sym - (. + 4)` 重定位；
  - 内核 UND weak 符号发 `SHN_ABS`（kallsyms 遍次一致性）；
  - 不发出未引用的 UND 符号；
  - `gen_call` 的 `args[16]` 溢出修复（narg 上限 64，覆盖形参致段错误）；
  - `src_load` 的 mmap 页尾 SIGBUS 修复（改 malloc+read）。
- **端到端证据（`/tmp/full_twopass.sh`）**：
  - 999/999 TU 由 c6c 编译（0 FAIL），替换全部 gcc `.o`；
  - `make vmlinux` 链接成功（无 Inconsistent kallsyms / undefined / multiple definition），
    vmlinux 172MB；
  - qemu 启动打印 `Linux version`、控制台、5 个 `NET: Registered`，走到预期的无 rootfs
    `VFS: Unable to mount root fs`（与 gcc 基线一致）。
- 注：`CC` 角色已消除，但构建的链接/汇编/host 工具仍用 gcc/binutils——见阶段 2–5。

## 阶段 2：HOSTCC——用 c6c 编 host tools（进行中）

- **已完成多个 host tools 的 c6c 编译链接**：
  - **kallsyms**（三 TU + elf-parse）：输出与 gcc 版 sysmap/.bin 逐字节一致，.S 仅 .incbin 文件名不同；
  - **fixdep**：对真实 .d 文件 stdout/depfile 修改逐字节一致；
  - **sorttable**（+ elf-parse）：对同一 vmlinux 排序后 ELF 逐字节一致；
  - **recordmcount**：可编译链接（本配置 `CONFIG_HAVE_FENTRY=y` 用 fentry 不走 mcount，故未做功能等价验证）。
  - **modpost**：4 个 host TU（modpost/file2alias/sumversion/symsearch）已用 c6c 编译成功；devicetable-offsets 需用 kbuild 真实命令编译。
  - 补齐的 libc 面：`strndup`、`perror`、`getopt`（短选项）、`getopt_long`（长选项，新增 `<getopt.h>`）、
    `scanf/sscanf/vsscanf`、`strsep`、`strchrnul`，以及 `<unistd.h>` 的 getopt 声明、
    `<stdbool.h>` 的 bool 类型无条件定义（内核 linux/stddef.h 只给 enum false/true 却不给 bool）。
- **kbuild HOSTCC 全链已打通**：`make HOSTCC=<c6c wrapper> -j1 scripts` rc=0，
  kbuild 自己用 c6c 编出 fixdep/mdp/gen_init_cpio/kallsyms/sorttable/asn1_compiler/
  tracepoint-update 并全部链接成功（多 TU 链接靠 c6c M_LINK 自动挂 rt.c6）。
  为此 c6c 补了 kbuild 固定带的 gcc 选项兼容：`-fomit-frame-pointer`、
  `-Wl,--fatal-warnings` 收下不生效，`-Wp,-MMD,<path>` 真正写出依赖文件；
  并修了 `include_next` 误命中——未给 `-B` 时不再把当前目录的 `include/`
  （内核根头）加进搜索路径，否则 sorttable 编不过。
- **剩余 host tools**：relocs、objtool（内核编译真正用到的部分尚未逐一验证）。
- 验证脚本 `c6c/tests/hostcc-tools.sh`：逐 TU 编译 + c6c 静态链接各工具，与 gcc 版逐字节对比输出。

## 阶段 3：AS——独立汇编器

- 实现 GNU as 兼容汇编器（预处理后的 `.s`：`.code64`/`.section`/`.macro`/`.rept`/符号
  与表达式/ELF .o 输出）。可复用 c6c 的 ELF writer（obj.c6）与 x64 编码器（x64.c6）。
- 75 个 .S 是边界，但大量共享宏（SYM_CODE_START/UNWIND_HINT 等），实际指令子集可控。
- **验证**：75 个 .o 与 gcc/as 版反汇编语义等价；链接后 vmlinux 能启动。

## 阶段 4：LD + AR/OBJCOPY——内核链接与归档工具

- 扩展 ld.c6：自定义段布局（vmlinux.lds.S 的 SECTIONS 语法）、--gc-sections、
  .a 归档、kallsyms 两遍链接、符号版本、orphan 段处理。
- 补 ELF 归档（ar）与段抽取（objcopy）。
  - **ar 子集已完成**（`c6c/src/artool.c6`）：`ar cDPrST` 建 thin archive、
    `ar cDPrsT` 暂按无索引处理、`ar t` 列成员、`ar q` 追加。覆盖内核 791 次
    无索引归档调用（这些中间归档不直接喂 ld，故不需 GNU 的符号索引）。
    生成的 thin archive 经 **GNU `ar t` 验证格式合法、成员列表一致**。
    CMake 目标 `c6c-ar` 用 c6c 自编译。符号索引（4 次带 s 的 built-in.a）
    仍由 GNU ar 处理。
  - **ar 构建链集成验证**：用 AR wrapper（无 s → c6c-ar，带 s → GNU ar）
    `make -j8 vmlinux` 全量重建，794 个归档中 791 次由 c6c-ar 生成；
    最终 **bzImage 逐字节不变**，qemu 启动打印 Linux version/Console/
    5×NET: Registered 并走到预期无 rootfs panic——与 gcc 基线一致。
  - **objcopy 子集已完成**（`c6c/src/objcopy.c6`）：`-O binary`（-R 排除 section、
    -S 忽略）支持 ELF32/ELF64 小端，摊平 PT_LOAD 为裸二进制。内核的
    setup.bin / realmode.bin / vmlinux.bin 三个输出与 GNU objcopy **逐字节一致**
    （17980 / 25188 / 15013376 bytes）。CMake 目标 `c6c-objcopy` 用 c6c 自编译。
  - **构建链集成验证**：用 `OBJCOPY=c6c/build/objcopy make arch/x86/boot/bzImage`
    重建，生成的 setup.bin/vmlinux.bin 与 gcc 基线逐字节相同，bzImage 不变；
    qemu 启动打印 `Linux version`/`Console:`/5×`NET: Registered` 并走到预期的
    无 rootfs panic——与 gcc 基线一致。objcopy 这个 binutils 角色已可替代。
- **验证**：`LD=c6c-ld` 链接出 vmlinux，qemu 启动一致。

## 阶段 5：端到端

- 全部 `CC/AS/HOSTCC/LD/AR/OBJCOPY` 指向 c6c 工具链，kbuild 不再调用任何 gcc/binutils。
- **验证**：干净环境（无 gcc）下产出可启动 vmlinux。

## 关键约束

- 每阶段独立可验证，不引入"全做完了才知道对不对"的大爆炸。
- 阶段 1（CC）已验证到 vmlinux+qemu；用 c6c .o 跑 `make bzImage` 会在
  `RELOCS arch/x86/boot/compressed/vmlinux.relocs` 报
  `Invalid absolute R_X86_64_64 relocation`。根因不是重定位类型，而是
  **顶层 asm 里的全局函数标签被 asmx 丢弃**：asmx 不支持 `endbr64` 指令、
  `.skip N, FILL` 的第二参数，导致 `DEFINE_ASM_FUNC`（8 处）整段 asm 作废，
  里面定义的 nop_func 等符号变成 UND weak→被发成 SHN_ABS→relocs 拒绝。
  已修这两处 asmx bug：nop_func 现在正确生成 `FUNC GLOBAL DEFAULT 1`，
  999/999 编译 + vmlinux 链接成功，nop_func 的 relocs 错误消除。
  又修了 `pushfq`/`popfq`（rethook SAVE_REGS_STRING 用）——最小复现里
  `arch_rethook_trampoline` 已能正确生成 `FUNC GLOBAL DEFAULT 1`。
  又修了 `pushq $symbol`（rethook fake return address）：asmx 现在把
  `$sym` 翻译成 `lea sym(%rip), %rax; pushq %rax`，复用 PC32 重定位；
  还支持 `$(3*8)` 这类简单算术立即数。`arch_rethook_trampoline` 已正确
  生成 `FUNC GLOBAL DEFAULT 1`。999/999 编译 + vmlinux 链接成功，fast 全绿。
  新障碍：tracepoint/static-call 的 `__SCK__tp_func_*` 符号（另一类顶层 asm）
  仍是 UND weak→SHN_ABS，bzImage 的 relocs 步报 Invalid absolute relocation。
  端到端（阶段 5）需继续按顶层 asm 种类逐个补齐 asmx。
- 阶段 2（HOSTCC kbuild 全链 + modpost）已打通；objcopy/ar 子集已完成并各自
  经构建链集成验证（bzImage+qemu）；剩余 AS、LD 未完成。
- 用户的完整目标尚未达成——中间产物（仅 CC、或 CC+kallsyms）是实质进展，不是终点。
