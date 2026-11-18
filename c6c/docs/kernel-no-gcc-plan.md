# 完全用 c6c 替代 gcc 构建 Linux 内核：分阶段路线

> **状态**：阶段 1（CC）已验证完成；阶段 2（HOSTCC/kallsyms）已打通；阶段 3–5 未开始 · **索引**：[文档索引](../../docs/README.md) · **关联**：[design.md](design.md) §15/§16、[CHANGELOG](../../CHANGELOG.md)

## 目标

完全用 c6c 替代 gcc/binutils 编译 Linux 内核，产出**可启动的 vmlinux**——消除
对 gcc（CC）、as（汇编器）、host tools 的 gcc 编译、GNU ld 链接的依赖。

## gcc 在内核构建里的 5 个角色（实测完整构建日志）

| 角色 | 调用数 | 工具 | c6c 现状 |
|---|---|---|---|
| CC | 3006 | `gcc -c` 编内核 .c | **999/999 TU 两遍发射编译成功，全量链接 + qemu 启动通过**（阶段 1 完成） |
| AS | 75 | `as` 编 `.S` | 无独立汇编器；需 GNU as 兼容前端或改写 75 个 .S（阶段 3） |
| HOSTCC | 6 | gcc 编 kallsyms/modpost/sorttable/fixdep/relocs | **kallsyms 三 TU 用 c6c 编译 + 链接成功，输出与 gcc 版逐字节一致**；其余 5 个未做（阶段 2 进行中） |
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

- **kallsyms 已完成**：`scripts/kallsyms.c` + `kallsyms-sysmap.c` + `elf-parse.c` 三 TU
  分别用 c6c 编译，与 `rt.c6` 一起链接（`-Wl,--allow-undefined` 忽略用不到的 `environ`）。
  补齐的 libc 面：`strndup`、`perror`、`getopt_long`（新增 `<getopt.h>`）。
  对同一 vmlinux 的输出：sysmap 与 .bin **逐字节一致**，.S 仅 `.incbin` 引用的文件名不同。
- **剩余 5 个 host tools**：modpost（依赖 libelf/gelf，`elf-parse.c` 已有基础）、
  sorttable、fixdep、relocs、objtool（C 实现但依赖较多）。
  验证口径：`HOSTCC=c6c` 重编后这些工具能跑对，最终 kbuild 不再调 gcc 编 host 工具。

## 阶段 3：AS——独立汇编器

- 实现 GNU as 兼容汇编器（预处理后的 `.s`：`.code64`/`.section`/`.macro`/`.rept`/符号
  与表达式/ELF .o 输出）。可复用 c6c 的 ELF writer（obj.c6）与 x64 编码器（x64.c6）。
- 75 个 .S 是边界，但大量共享宏（SYM_CODE_START/UNWIND_HINT 等），实际指令子集可控。
- **验证**：75 个 .o 与 gcc/as 版反汇编语义等价；链接后 vmlinux 能启动。

## 阶段 4：LD + AR/OBJCOPY——内核链接与归档工具

- 扩展 ld.c6：自定义段布局（vmlinux.lds.S 的 SECTIONS 语法）、--gc-sections、
  .a 归档、kallsyms 两遍链接、符号版本、orphan 段处理。
- 补 ELF 归档（ar）与段抽取（objcopy）。
- **验证**：`LD=c6c-ld` 链接出 vmlinux，qemu 启动一致。

## 阶段 5：端到端

- 全部 `CC/AS/HOSTCC/LD/AR/OBJCOPY` 指向 c6c 工具链，kbuild 不再调用任何 gcc/binutils。
- **验证**：干净环境（无 gcc）下产出可启动 vmlinux。

## 关键约束

- 每阶段独立可验证，不引入"全做完了才知道对不对"的大爆炸。
- 阶段 1 已完成；阶段 2 的 kallsyms 已打通；剩余 host tools、AS、LD/AR/OBJCOPY 未完成。
- 用户的完整目标尚未达成——中间产物（仅 CC、或 CC+kallsyms）是实质进展，不是终点。
