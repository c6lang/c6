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
  新障碍 __SCK__tp_func_*（tracepoint 顶层 asm）曾导致 relocs 报错；
  根因是 UND weak→SHN_ABS 的符号仍带 R_X86_64_64 数据重定位（内核 relocs
  拒绝 ABS 符号的绝对重定位）。已修：保留 ABS 符号表项（kallsyms 遍次一致
  性需要），但对 pseudo 符号跳过 R_X86_64_64 重定位——SCK 错误已消除，
  999/999 编译 + vmlinux 链接成功，fast 全绿。
  新障碍 paddr_vmcoreinfo_note：__weak 函数定义在多个 TU 里被 UND weak→
  SHN_ABS 0 的占位符号抢占（GNU ld 对同名 weak 定义"第一个胜出"），
  EXPORT_SYMBOL 的地址重定位因此指向 ABS 0，被内核 relocs 拒绝。
  根因诊断：full_twopass 的 make vmlinux 用的是 GNU ld（不是 c6c ld.c6），
  在 ld.c6 里改 weak 覆盖逻辑无效（已回退）。关闭 ABS 化会令 kallsyms
  多遍地址不收敛（KALLSYMS_EXTRA_PASS 只加一遍，不能解决），不可用。
  已修：DFIX/NOP 化 + ret stub——UND weak 引用不再发成 SHN_ABS 0，
  而是在 .text 末尾生成独立 `ret` stub，pseudo 符号统一指向它；ABS WEAK
  符号从 140 降到 0，x86_64_start_kernel 的 `call 0` 已消除。
  make bzImage 全 c6c 工具链再次成功（35,776,000 字节），fast 全绿。
  修复 c6c 核心 bug：常量表达式 `~` 对二元右操作数会读到错误的 CONST_TY，
  导致 `~(1UL<<63)` 算成全 1（应 0x7fff…）——内核 early_pmd_flags 多出 NX 位。
  已修 const_mul/const_add/const_shift 的类型恢复；最小复现与 gcc 完全一致。
  999/999 编译 + vmlinux 链接成功；early_pmd_flags 现为正确的 0xe3；bzImage
  全 c6c 工具链构建成功（35,776,000 字节）；fast 全绿。
  **启动失败根因已确认并修复**（2026-11-19）：DCE=2 下 UND 数据符号
  （DFIX 初始化目标如 `current_task = &init_task` 的 init_task）被无条件
  转成 pseudo/WEAK-ABS，导致 DFIX 的 R_X86_64_64 初始化重定位被整条跳过，
  current_task 数据格留在 0 → common_startup_64 读到 0 → 无效 %rsp →
  triple fault。修复（obj.c6）：DFIX 目标中真正的外部符号保持 GLOBAL UND；
  仅 `__SCK__tp_func_*`（tracepoint stub，无定义、gcc 优化掉）仍转 pseudo
  ——它必须在 System.map 里保持非零 stub 地址，否则 kallsyms 一致性检查失败。
  修复后：current_task=0xffffffff89808a40（有效），999/999 编译 + vmlinux
  链接 + kallsyms 一致 + c6c 工具链 bzImage 全通过；qemu in_asm 显示内核已从
  triple fault 推进到 x86_64_start_kernel() 调用链返回、触发 head_64.S 的
  ud2（该函数源码标注 __noreturn，不该返回）。
  第二个 DCE=2 bug（已修复）：obj_dce_stub_dead 把所有 UND 非 static 符号的
  call 都 NOP 化（head64.o 的 PLT32=0），包括有真实定义的 start_kernel
  （在 gcc 编译的 init/main.o）——x86_64_start_reservations 没调用它就 ret →
  ud2。修复：start_kernel / x86_64_start_reservations 保持 GLOBAL UND 且 call
  保留。修复后内核推进到 tracepoint 的 `call *%r11`（22711 条指令，无 ud2），
  最后的间接调用未返回——疑为 KASLR 下 tracepoint 函数指针（.data 里的
  __SCK__ stub 地址）未重定位。
  **否定 KASLR 假设**：nokaslr 对照实验（in_asm）同样停在 `call *%r11`
  （nokaslr 地址 0x83140cb1 / KASLR 0xa6740cb1，同一指令），排除重定位问题。
  反汇编定位：该 `call *%r11` 在 cgroup_init_early（gcc 编译的 cgroup.c，
  不在 c6c 编译列表）——不是 c6c codegen bug，而是 gcc 代码引用了被 c6c
  生成代码/数据破坏的状态（函数指针数组元素无效）。剩余：用 -d int 抓崩溃
  异常类型。#PF(CR2=0)→#GP→#GP 确认 `call *%r11` 跳地址 0（RIP=0、R11=0）。
  -d in_asm 显示 r11 来自栈槽 -0xe30(%rbp)，该槽在 gcc 代码里被存成
  `lea 0x0(%rip)` 的有效地址（对照 gcc 内核正常）；运行时却是 0——疑似
  c6c 生成的代码在低栈地址写坏了该槽，或 static_call 函数指针未初始化。
  剩余：用 gdbstub 在 0x83140bfb 下断点看 -0xe30 槽何时被清零。
  **关键对照**（gcc 基线 vmlinux）：同位置 gcc 生成的是
  `call __SCT__WARN_trap`（直接调用，地址有效）；c6c 内核里变成
  `lea 0x0(%rip),%rax; call *%r11`——**cgroup.c 其实在 c6c 编译列表里**
  （我先前误判为 gcc 编译），所以这是 c6c codegen：把 gcc 的 `WARN` 宏里
  `call __SCT__WARN_trap`（真实直接调用）错误地生成了 `call *%r11`。
  c6c cgroup.o 里 `UND __SCK__WARN_trap` 是罪魁：DCE=2 下该 UND 符号被转
  pseudo/stub 0，`call *%r11` 的函数指针=0 → #PF(CR2=0)。
  **推翻 static_call 假设**：用当前修复后的 c6c 重新链接验证，
  `__SCK__WARN_trap` 数据第一个 qword = 0xffffffff88aa3a40（__WARN_trap
  有效地址），static_call 初始化正常。但其后启动仍在同一个 `call *%r11`
  （#PF CR2=0、R11=0）崩溃——与 static_call 无关。
  **真正的 codegen bug**：c6c cgroup.o 里大量 `lea 0x0(%rip),%rax`（无重定位），
  它取的是「下一条指令地址」而非目标符号（如 __warn_printk/函数指针）。
  崩溃序列：0x83140bf4 `lea 0x0(%rip),%rax` → 存 -0xe30 → 0x83140c99
  `mov -0xe30(%rbp),%r11` → `call *%r11` 调用指令自身 → 无限递归/栈溢出 →
  RIP=0 #PF。c6c 后端对函数/全局符号取址错误生成 RIP-relative 0 偏移，
  丢失符号重定位。
  **最终对比证据**：gcc 基线访问 current_task 是
  `mov %gs:0x25bfe2c(%rip),%rax # current_task`（per-CPU 段 %gs + RIP 相对
  重定位）；c6c 对应代码是 `lea 0x0(%rip),%rax; mov 0x0(%rax),%rdx`
  ——缺 %gs 段前缀、displacement=0、无重定位。根因：c6c 后端对 per-CPU
  /全局符号取址生成了错误的 RIP-relative 0 偏移且丢失重定位，这是启动
  失败的根本 codegen bug。剩余：在后端（符号取址 lowering）定位并修复。

  **本轮 codegen 修复（2026-11-19，已验证推进启动）**：
  1. `obj.c6` DCE 压缩 fixup 时，只应 NOP 化 E8 call 的 UND 引用；原先无条件
     drop 所有 UND fixup，导致 lea（per-CPU 符号取址）和 `.long sym-(.+4)`
     （static_call trampoline jmp）的 disp32 停在 0。改为 `is_call` 判断。
  2. `asmx.c6` 顶置的 `.pushsection .discard...` 元数据段（ANNOTATE_NOENDBR）
     未正确吞掉，`.long 911b - ., N` 被拆成两个 long 发到 .text，在
     `__SCT__WARN_trap` 的 jmp 前留下 8 字节垃圾。改为整条丢弃数字标签引用。
  验证：999/999 编译 + 链接 + bzImage 全通过；qemu in_asm 显示 `call *%r11`
  现在正确解析到 __SCT__WARN_trap → `jmp __WARN_trap` → `ud1`（WARN trap）。
  内核从"跳地址 0 崩溃"推进到"WARN 触发 → #UD → #GP → triple fault"。
  剩余：WARN 在正常启动中不该触发（cgroup_init_early 条件判断有误），且 #UD
  处理本身跳 #GP 崩溃。


  **后续诊断（WARN trap → #UD）**：call *r11 → __SCT__WARN_trap → jmp
  __WARN_trap → `ud1 (%edx),%arg1`。-d int 显示 #UD(v=6) 后立即 #GP
  (e=0x32: IDT=1, index=6)，RIP 仍是 ud1 地址——CPU 无法通过 IDT 门 6
  deliver #UD 到 asm_exc_invalid_op。疑点：c6c 生成的 IDT traps 表
  （arch/x86/kernel/idt.c / traps.c 的 early_idts / idt_setup_traps）
  中 vector 6 的门无效，或 asm_exc_invalid_op 地址错误。剩余：查 IDT
  #UD 门设置。


  **关键 DCE 修复（2026-11-20）：只 NOP weak UND 调用**
  obj_dce_stub_dead 原先 NOP 所有 UND 非 static 的 E8 call（仅白名单
  start_kernel 等例外）。但 GLOBAL UND 是别的 TU 有真实定义的外部函数，
  把它们 NOP 掉会摧毁整条初始化调用链：trap_init() 里的
  `idt_setup_traps()` 调用被 NOP → IDT 表运行时全 0 → 所有异常投递失败
  （#UD/#PF → #GP → triple fault）。修正：只 NOP **weak** UND call
  （真 pseudo/tracepoint stub，链接后是 ABS 0）；GLOBAL UND 全部保留交
  链接器解析。验证：traps.o 恢复 idt_setup_traps PLT32 重定位（386 条
  vs 342）；999/999 编译+链接+bzImage 通过；qemu 启动从 WARN trap 推进
  到内存拷贝循环（21127 指令），#PF(CR2=ffff888000014770) 在
  setup_cpu_entry_areas 类代码处，#PF 处理再 #DF。剩余：查该 #PF 的
  无效地址来源（copy 循环 rsi/rdi 指针）。


  **精确定位 #PF（2026-11-20）**：in_asm+int 追踪，崩溃在 `memcpy_orig`
  （arch/x86/lib/memcpy_64.S，汇编非 c6c），指令 `mov (%rsi),%r8`，
  CR2=ffff888000014770（direct map 物理 0x14770），#PF error=0（not-present
  read）。调用者是 `copy_bootdata`（arch/x86/kernel/head64.c:194）的
  `memcpy(&boot_params, real_mode_data, sizeof(boot_params))`，src=real_mode_data
  =__va(0x14770)。KASLR 偏移 0x1e200000（运行 0xa6ca1fa6 ↔ vmlinux 0x88aa1fa6）。
  疑点：0x14770 这页未映射（早期 direct map 页表缺失），或 real_mode_data
  物理地址值被 c6c 早期代码算错（gcc 内核该地址有效）。剩余：查 real_mode_data
  来源（head_64.S 传参）与早期 direct map 页表建立。


  **参数链确认（2026-11-20）**：反汇编 x86_64_start_kernel → copy_bootdata
  的调用点，确认 `__va(real_mode_data)` 计算正确：rdi = real_mode_data(物理
  0x14770) + page_offset_base(ffff888000000000, vmlinux 初始值已验证) =
  ffff888000014770。参数传递无误，#PF 是运行时该 direct map 页 truly 未映射
  （早期页表只覆盖内核镜像区，boot_params 页 0x14770 未纳入）。gcc 内核靠
  早期页表映射该页而 c6c 内核镜像布局不同（bzImage 35MB vs 15MB）可能导致
  早期映射范围差异。剩余：查 head_64.S 早期页表建立 / init_mem_mapping 时机。
