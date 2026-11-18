# 完全用 c6c 替代 gcc 构建 Linux 内核：分阶段路线

> **状态**：规划（goal-3） · **索引**：[文档索引](../../docs/README.md) · **关联**：[design.md](design.md) §15/§16、[CHANGELOG](../../CHANGELOG.md)

## 目标

完全用 c6c 替代 gcc/binutils 编译 Linux 内核，产出**可启动的 vmlinux**——消除
对 gcc（CC）、as（汇编器）、host tools 的 gcc 编译、GNU ld 链接的依赖。

## 现状：gcc 在内核构建里的 5 个角色（实测完整构建日志）

| 角色 | 调用数 | gcc/binutils 具体工具 | c6c 现状 | 缺口 |
|---|---|---|---|---|
| CC | 3006 | `gcc -c` 编内核 .c | 999/999 零诊断；6 个简单 TU 可链接进 vmlinux | **inline 全量发射**导致符号冲突（`native_save_fl` 等）+ 70–1200× 膨胀 |
| AS | 75 | `as` 编 `.S`（预处理后含 GNU as 宏/伪指令） | c6c 无独立汇编器（只有内联汇编 asmx + `-S` 文本输出） | 需 GNU as 兼容的汇编器或改写 75 个 .S |
| HOSTCC | 6 | gcc 编 kallsyms/modpost/sorttable/fixdep/relocs | 能编静态/动态 ELF，自带 libc 356 个函数 | 缺 `pthread_*`/`regexec`/`fnmatch`/`__ctype_b_loc` 等；modpost 依赖 libelf/gelf |
| LD | 10 | `ld` 按 vmlinux.lds.S 链接 | ld.c6 静态链接器（.text/.data/.bss + PC32/PLT32/64/32S） | 不支持自定义段布局、--gc-sections、.a 归档、符号版本、kallsyms 两遍链接 |
| AR/OBJCOPY | 800+ | `ar` 归档、`objcopy` 抽取 | 无 | 需 ELF 归档与段抽取工具 |

## 分阶段路线（先易后难 + 每阶段可独立验证）

### 阶段 1：CC 全量替换——消除 inline 符号冲突与膨胀
- **问题**：c6c 不做内联展开，内核头里上万个 `static/extern inline` 全量发射成全局
  符号，`native_save_fl` 等与真实定义 multiple definition；main.o 膨胀 70×。
- **已做（增量 1，commit 026a694）**：
  - B. **inline 符号本地化**：kernel 模式下 inline 函数发射名带 TU 后缀（像 static），
    消除 multiple definition。局部 static（`task_index_to_char.4`）也补了 STB_LOCAL。
  - 实测：之前冲突的 4 个 TU 符号正确本地化，fast 全绿。
- **剩余核心障碍：函数级 DCE**（下一步）。实测数据：
  - c6c 编 rdrand.o 有 **1425 个 local 函数**（gcc 0 个）——全是未内联的 inline 函数体；
  - c6c 有 **740 个未定义符号**，gcc 只有 3 个——737 个是死代码引用；
  - 死符号引用（如 `__xadd_wrong_size`）都在 `arch_atomic_*` 这类 **local inline 函数内**
    （GCC 内联/消除它们，c6c 全量发射）。
  - 结论：**函数级 DCE（剔除未被调用的 local 函数）即可消除绝大多数死符号**，不需要
    复杂的常量折叠/分支消除。
- **DCE 实现方案**：c6c 当前是单遍编译（parse.c6:22410 边解析边 `x64_compile`），
  需在发射前知道函数是否被引用。两条路：
  - 两遍：先解析全部函数收集 IR + 引用图，从全局符号做可达性分析，再发射可达函数。
    工作量大但彻底。
  - 延迟发射：local 函数先缓存 IR，TU 结束按引用计数决定发射。需改 x64 镜像布局。
- **验证**：全 999 个 TU 用 c6c 编译后，kbuild 链接 vmlinux 不再 undefined reference；
  qemu 启动与 gcc 基线一致。

### 阶段 2：HOSTCC——用 c6c 编 host tools
- 补 libc 缺口：`pthread_create/join/exit`、`regcomp/regexec/regerror`、`fnmatch`、
  `__ctype_b_loc`、`strndup`、`perror` 等（c6c 已有 strtol/qsort/malloc 基础）。
- modpost 的 libelf/gelf 依赖：要么给 c6c 补 ELF 读取 API，要么替换 modpost 实现。
- **验证**：`HOSTCC=c6c` 重编 6 个 host tools，kallsyms/sorttable/modpost 能跑对。

### 阶段 3：AS——独立汇编器
- 实现 GNU as 兼容汇编器（预处理后的 `.s`：`.code64`/`.section`/`.macro`/`.rept`/符号
  与表达式/ELF .o 输出）。可复用 c6c 的 ELF writer（obj.c6）与 x64 编码器（x64.c6）。
- 75 个 .S 是边界，但大量共享宏（SYM_CODE_START/UNWIND_HINT 等），实际指令子集可控。
- **验证**：75 个 .o 与 gcc/as 版反汇编语义等价；链接后 vmlinux 能启动。

### 阶段 4：LD——内核链接器
- 扩展 ld.c6：自定义段布局（vmlinux.lds.S 的 SECTIONS 语法）、--gc-sections、
  .a 归档、kallsyms 两遍链接、符号版本、orphan 段处理。
- **验证**：`LD=c6c-ld` 链接出 vmlinux，qemu 启动一致。

### 阶段 5：端到端
- 全部 `CC/AS/HOSTCC/LD/AR/OBJCOPY` 指向 c6c 工具链，kbuild 不再调用任何 gcc/binutils。
- **验证**：干净环境（无 gcc）下产出可启动 vmlinux。

## 关键约束
- 每阶段独立可验证，不引入"全做完了才知道对不对"的大爆炸。
- 阶段 1 最关键（CC 是最大头），且已有基础（999/999 + 6 TU 链接成功）。
- 用户目标是"完全 c6c 替代"，但每阶段的中间产物（如只有 CC 被替换）也是进展。
