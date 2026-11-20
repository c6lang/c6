# c6c 内核启动诊断记录（2026-11）

## 已修复并合入 main 的 c6c 编译缺陷
- bsr/bsrq、tzcnt/bsf、bts/popcnt 指令编码与 ALTERNATIVE popcnt 提取
- static UND 符号不再发成 STB_LOCAL UND（曾导致 `call 0`）
- 空声明布局、内联 asm `+r` 输出写回变量真实帧槽、CR/端口 IO 识别
- 跨 TU 别名 / static inline / 8 字节 struct 返回 ABI
- PIE / 栈对齐 / 重定位 / 段对齐启动修复
- 保留 WASM 后端与 C99_WASM_NARGS
- **复合字面量零初始化**：表达式位置 `(T){...}` 先 `gen_zero_obj(addr, ty_size(ty))`
  再铺初始化器（ee62fba）。未提及成员不再残留栈垃圾。修复了 SLUB
  `pool_workqueue` 的 `ctor=0x8` 早期 #PF，纯编译器修复、无内核源码 workaround。
- **真实 call 目标发 WEAK UND**（f2a72d6）：内核模式 DCE 原来把 UND weak
  函数引用折叠成指向 `ret` stub 的 WEAK **定义**。当真实定义在别的 TU 也是
  WEAK（如 jiffies.c 的 `clocksource_default_clock`）时，GNU ld 在多个 weak
  定义里挑到 ret stub，`clocksource_default_clock()` 返回栈垃圾
  `0xffffffff84800ba8`，`tk_clock_read` 的 `call *%r11` 跳进去 #PF，
  `console_init` 永远到不了。现在 DCE 压缩后仍存活的 E8 call 目标发成真正
  的 WEAK UND（shndx=UNDEF），由链接器解析真实定义；data/非调用引用仍用 ret stub。
- **NOP 只声明不定义的 `__bad*` 助手**（7d0e92f）：`__bad_size_call_parameter`
  等是 gcc 折叠掉的不可达分支助手，c6c 保留了调用；普通 extern（is_weak=0）
  不触发原有 weak-call NOP 规则，WEAK UND 后解析成 0，故显式 NOP。
- **两遍发射可达集按名字兜底**（78504f5）：前向声明 `static bool f();` 与后面
  定义落成不同符号 id，调用点指向 UND 的那个 → 定义被 DCE 当死函数剔除 →
  链接后 `call 0`。内核 `page_contains_unaccepted`（CONFIG_UNACCEPTED_MEMORY
  关闭的 `#else` 分支）即此形。IR_CALL 目标无直接 def 映射且为 UND 时，按名字
  （含 TU 前缀后缀）兜底标记同 TU 的 static 定义可达。page_alloc.o 里该符号
  从 `U` 变 `t`，`call 0` panic 消失。

验收：`cmake --build out --target c6c -j8` 通过；`ctest -R c6c-tests2`
失败项与基线一致（119_random_stuff / 138_arm64_encoding / 61_integers /
95_bitfields / 98_al_ax_extend / 99_fastcall），无新增回归。

## 已达成
- 全内核 C 源码可由 c6c 编译、链接出 vmlinux/bzImage（链接仍用 GNU ld）。
- 最小全 c6c 内核在 QEMU（`-cpu Nehalem`）串口输出含 `Linux version`。
- 弱符号修复后**真实 console 工作**：串口从 560 字节增至 5725 字节，
  出现 `mem auto-init` 等真实 boot 日志（此前只有早期 COM1 直写的一行 banner）。
- **GNU ar 依赖已消除（最小构建）**：内核 vmlinux 最终链接用
  `ld --whole-archive`，逐成员读取、不需要归档符号索引。因此把 kbuild 的
  `AR` 包装器改成全部转发 c6c 自带 `ar`（原 `s` 选项回退 GNU `/usr/bin/ar`
  是多余的）：最小全 c6c 树 `vmlinux.a`（thin、无 `/` 索引成员）由 c6c ar
  生成，`ld -r --whole-archive` 链接成功，bzImage 启动串口仍 5779 字节含
  `Linux version`。即最小构建链里 ar 环节不再需要 GNU binutils。

## 未达成 / 根因已定位
1. **内核仍未真正可启动**：`call 0` 与弱符号 fault 已全部消除，当前在
   `get_freepointer`（slub.c）的 `rep movsb` 读 freelist 指针处 #PF，
   CR2=`0x00000008001530f0`。坏指针低 32 位 `0x001530f0` 正常、高 32 位
   变成 `0x00000008`（应 `0xffff8880`）——这是最初就有的深层 SLUB
   freelist 高级字破坏（先前用命名局部绕过 ctor=0x8 时它也出现）。
   复合字面量、弱符号、可达集三处编译器缺陷修复后它成为最后一个拦路 bug。
   **根因已用条件硬件观察点定位**：坏值 `0x00000008001530c0` 被
   `__alloc_workqueue+0x2936` 的 `mov %edx,0x0(%rax)` 写入（rax=
   `0xffff888000153094`，即 freelist 槽的高半字；edx=8）。这是
   `pool_workqueue` 对象的一个合法 int 字段写入——说明对象
   `0xffff888000153060`（freelist 槽在 +0x30）**同时挂在 SLUB freelist 上
   又被分配给了 workqueue**：双重分配 / 陈旧 freelist。问题在 c6c 的 slab
   快路径 fastpath（this_cpu cmpxchg 弹 freelist 时没有正确推进），而不是
   单纯的存储宽度错误。需要检查 `slab_alloc_node` 快路径的 cmpxchg 序列。
   更深层根因（同一 section 属性缺陷）：c6c 构建的 vmlinux 里 `.data..percpu`
   段是空的（size=0、0 个符号），GCC 基线同段 0x2b858 字节 / 4096 对齐；
   c6c 产物里 `%gs:`/`%fs:` 前缀指令数均为 0（GCC 基线 54261 条 `%gs:`）。
   DEFINE_PER_CPU 靠 section 属性落 `.data..percpu`，被丢弃后所有 per-CPU
   变量（含 SLUB 的 cpu_sheaves）丢失/错位 → 快路径拿到错误 sheaf → 同一
   对象被分配两次。双重分配是「section 属性丢失」的下游症状。更根本的是 **c6c 完全不支持
   `%gs:` 段前缀**：独立复现 `asm volatile("mov %%gs:0, %0" : "=r"(v))` 时
   c6c 静默丢弃整条 asm（不发指令、变量保持 0），全镜像 `%gs:`/`%fs:` 均为
   0 条。内核 `CONFIG_CC_HAS_NAMED_AS=1` 时 per-CPU 读写全靠 GCC 生成 `%gs:`；
   即使 `-U CONFIG_CC_HAS_NAMED_AS` 走 `__per_cpu_offset` 路径仍 panic
   （NULL 解引用）。修 per-CPU 需在 asmx 增加 `%gs:`/`%fs:` 前缀编码 +
   段寄存器操作数解析——比命名段更大的工程。

2. `__attribute__((section("...")))` 被 c6c 的 `skip_attributes()` 静默丢弃
   （parse.c6 的 `sa_is_kw` 表无 `section`，属性体扫描无对应分支，落到通用
   `pa_advance()`）。`.init.setup` / `.earlycon_table` / `__X_sched_class` /
   `.initcall*` 全部落进 `.data`，earlycon/console 启动参数注册失效。
   修复需给 obj.c6 增加可变命名段（见下）。

3. **工具链依赖未消除**：
   - c6c 没有独立汇编器；`.S`（含 ELF32/16 位实模式）仍用 GNU as。
   - c6c 的 ld.c6 仅 ELF64 静态链接、最多 64 对象，不支持链接脚本、`.a`、
     `--gc-sections`、kallsyms 多遍、ELF32；内核链接仍用 GNU ld。
   - host tools（modpost/relocs/objtool）仍由 GCC 构建。
   这些是全新实现，单次会话剩余轮次无法完成。

## 建议后续顺序
1. 链接期处理无定义 weak 调用（或扩充不可达助手的 NOP 名单），消除 `call 0`。
2. 给 obj.c6 增加命名段（变量优先，约 150-220 行，追加在 19 之后避免重编号）：
   - `SAW_SECTION` 捕获 + `X64Sym.sect_idx/size`；
   - `ObjSec` 描述符数组替代固定 19 项表；
   - 从 DATA_IMG 挖出具名字节并重写 `st_value` / `r_offset`。
3. 逐段排查后续启动 fault。
4. 新建汇编器与内核级链接器（最大工作量）。

## 更正：per-CPU 根因判断对当前最小树不成立（第 33 轮）
上文把启动 panic 归因于「section 属性丢失 → `.data..percpu` 空 → per-CPU
错位」以及「缺 `%gs:` 段前缀」，这对当前最小树不成立：该树是 UP
（`CONFIG_SMP` 未定义、`CONFIG_NR_CPUS=1`），percpu.h 的 UP 路径
`__my_cpu_ptr(ptr) = (ptr)` 是普通指针，不用 `%gs:`；实测 `alloc_from_pcs`
中 `%gs:` 计数为 0、全镜像无 `__per_cpu_offset`。所以空的 `.data..percpu`
与缺 `%gs:` 在 UP 下都不直接致错，双重分配真正成因仍未定位。
已通过原生复现/反汇编排除：sheaf `size--` 回填正确；freelist encode/decode
正确；`__update_freelist_slow` 的 `slab->freelist=NULL` 是正确 64 位存储；
窄化高字校验与命名 AS config 开关均无效。命名段支持仍是 c6c 真实缺口
（影响 `.init.setup` 等），但不是当前 panic 的直接原因。

## 第 34 轮：GDB 捕获坏 freelist 指针的精确调用路径
用 QEMU `-s -S` + GDB 在 `get_freepointer` 入口设条件断点
（`($rsi>>32)==8`），捕获到坏对象 `0x00000008001530c0` 的完整调用栈：
```
#0  get_freepointer
#1  alloc_single_from_partial
#2  get_from_partial_node
#3  get_from_partial
#4  ___slab_alloc
#5  slab_alloc_node
#6  __do_kmalloc_node
#7  __kmalloc_noprof
```
即坏指针不是来自 per-CPU sheaf，而是来自 **partial slab 的 `slab->freelist`**。
`alloc_single_from_partial` 里 `object = slab->freelist` 读到
`0x8001530c0`（低半正确、高半被写成 8），随后
`slab->freelist = get_freepointer(s, object)` 的 `rep movsb`
从 `0x8001530f0` 读而 #PF。反汇编确认：
- `freelist_ptr_encode` 代码正确（零填返回 temp + 64 位存 ptr）；
- `set_freepointer` 里 `mov 0x0(%rax),%edx` 是加载 32 位 `unsigned int
  s->offset`（偏移 0x28），正确；
- `CONFIG_SLAB_FREELIST_HARDENED` 未启用，encode 就是纯指针拷贝。
下一个待查点：是谁把高半字 8 写进了那个对象的 freelist 槽
（先前观察点指向 `__alloc_workqueue` 的 int 字段写 rax=0xffff888000153094、
edx=8，需确认这是否为合法对象数据写 → 双重分配，还是 c6c 存储宽度错误）。

## 第 36/37 轮：%gs: 段前缀已实现（更正上文「不支持」）
asmx.c6 现已支持 `%gs:disp` / `%fs:disp` 段覆盖前缀（commit `665962b`）：
`AxOp.seg` 记录 0x65/0x64，`ax_emit_rm` 在 REX 之前发前缀字节，裸位移转成
绝对 disp32 内存操作数。实测三种形式均正确：
- `movq %%gs:0x10,%%rax` → `65 48 8b 04 25 10 00 00 00`
- `movq %%gs:0x10(%%rip),%%rax` → `65 48 8b 05 ...`（RIP 相对）
- `movq %%gs:(%%rax),%%rax` → `65 48 8b 00`（基址）
c6c-tests2 无新增回归（仍 5 条已知意外失败 + 99_fastcall 运行期信号）。
注意：当前最小树是 UP，per-CPU 走普通指针不用 `%gs:`，故此修复不直接改变
最小树的启动行为；它对 CONFIG_SMP 内核（完整树）的 per-CPU 访问是必需的。

## 第 37 轮：SMP 全树 per-CPU 编译路径已打通（实验验证）
完整树（/tmp/linux-7.4-rc3）是 CONFIG_SMP=1 + CONFIG_CC_HAS_NAMED_AS=1，
per-CPU 走命名地址空间（__seg_gs），c6c 把 __seg_gs 当 no-op → per-CPU 全错。
实验：临时 undef autoconf.h 里的 CONFIG_CC_HAS_NAMED_AS，让内核回退到
`%%gs:` 内联汇编路径，用新 c6c（含 %gs: 段前缀支持）编译
`kernel/sched/core.c` 成功，产出 **27 条 `%gs:` 指令**，例如：
```
65 4d 0f b1 08   cmpxchg %r9,%gs:(%r8)     (this_cpu_cmpxchg)
65 49 ff 02      incq %gs:(%r10)           (this_cpu_inc)
65 4d 21 1a      and %r11,%gs:(%r10)
```
前缀字节 0x65 在 REX(0x4d) 之前，编码正确。autoconf.h 已恢复。
这给出一条可行路线：**SMP 内核用 c6c 编译时需关闭
CONFIG_CC_HAS_NAMED_AS**（因 c6c 无命名地址空间语义），配合已实现的
`%gs:` 段前缀，per-CPU 访问即可正确生成。全树 2424 个 C 文件的完整
重编/链接/启动留待后续（仍需命名段 + 内核级链接器）。

## 第 37 轮：全树链接剩余障碍 = DCE 丢弃「仅被数据引用」的 static 函数
用新编译器（含 %gs:）+ 关 named-AS 重编全树 2424 个 C 文件成功，
`vmlinux.o` 含 24577 条 `%gs:` 指令（per-CPU 已正确）。
补 399 个 `__SCT__` 桩后，最终 ld 链接仍剩 **214 个未定义符号**，
例如 `thermal_genl_event_tz_delete`、`acpi_button_resume`、
`console_callback`、`blank_screen_t`。这些在整个 vmlinux.a 里都只有 U、
没有定义——它们是各 .c 里的 static 函数，只通过**数据中的函数指针**
（file_operations / work_struct / notifier 等结构）被引用，没有直接 call
边。c6c 两遍 DCE 的可达性只沿调用图 + 直接数据重定位走，没把这类
「仅数据引用」的 static 函数纳入可达集，于是整函数被剔除 → 链接 undefined。
这是 kernel-mode DCE 的系统性缺口：可达性分析需要把所有**被重定位引用
的符号**（R_X86_64_64/PC32 的目标）都当作 root，而不只是 E8 call 目标。
附加问题：ld 报 orphan `.eh_frame` 无放置（链接脚本严格 orphan-handling）。

### 具体佐证（第 37 轮）
`drivers/thermal/thermal_netlink.c:273` 的
`static int thermal_genl_event_tz_delete(struct param *p)` 只出现在数据数组
`thermal_genl_cmd[]`（第 293 行）的函数指针里，无直接 call 边。该文件经
c6c 编译后，`nm drivers/thermal/thermal_netlink.o` 显示该符号为 `U`（未定义）
而非定义——两遍 DCE 把它剔除了。其余 213 个未定义符号同类：都是 static
函数/数据仅被数据段函数指针引用。修复方向：`pa_emit_deferred` 的可达根需
额外纳入「被数据重定位（DFIX/IR_ADDR 之外的数据初始化）引用的符号」，
或在 obj 层把这类 UND 按名字在产生它的 TU 内找定义并保留。

## 第 37 轮重大进展：全树 vmlinux 成功链接（重大里程碑）
两项 DCE 修复（数据引用 static + alias 目标 static，commit `023aa30`）落地后：
- 全树非 SCT 未定义符号从 **214 → 16**（剩余 16 个全是链接脚本定义的边界符号
  `__initcall*_start`、`__start_orc_unwind`、`init_stack` 等，由 vmlinux.lds 解析）。
- 补 399 个 `__SCT__` 桩后，`ld -T arch/x86/kernel/vmlinux.lds` 成功产出
  **410MB 的 x86-64 ET_EXEC vmlinux**（ELF 64-bit LSB executable, statically linked）。
- `vmlinux.o` 含 24577 条 `%gs:` 指令，per-CPU 访问已正确。
- 唯一残留：`--orphan-handling=warn` 下 `.eh_frame` 被放到 `.eh_frame`（无害，
  `--fatal-warnings` 才报错）。
- bzImage 尚未产出：`make bzImage` 会重新 LD vmlinux 并再次触发 `.eh_frame`
  strict orphan 错误；需手动 objcopy → gzip → mkpiggy 路径，且 kallsyms 多遍
  构建仍需 GCC 工具链参与。

## 第 38 轮：全树 bzImage 首次构建成功 + 启动到内核入口
### 构建路径（全部手动，规避 make 重链 kallsyms）
1. `objcopy -R .comment -S vmlinux vmlinux_elf.bin`（**关键**：必须保留 ELF 头，
   先前误用 `-O binary` 导致 decompressor 报 "Kernel is not a valid ELF file"）
2. `gzip -n -c` → `mkpiggy` → `gcc -c piggy.S`（piggy.o 是唯一 GCC 产物，仅为 decompressor 桩）
3. 用内核 `.vmlinux.cmd` 里的**原始 ld 命令**链 compressed/vmlinux（含 libstub/lib.a、startup/lib.a）
4. `objcopy -O binary` → `dd setup.bin bs=4k` + `cat vmlinux.bin` → bzImage（75.5MB）
### 启动结果
```
Decompressing Linux... Parsing ELF... Performing relocations... done.
Booting the kernel (entry_offset: 0x167dc7e0).    <- 进入内核入口后崩溃
```
`nokaslr` 路径反而在 decompressor 里 "uncompression error"（次要问题）。
### GDB + QEMU -d int 定位早期崩溃
- 双重故障 `#DF`（`check_exception old: 0xe new: 0xe`），RIP `0xffffffff8394eede`
  （在 `lockdep_hardirqs_off` 的 `push %r12` 中途），RBP 是**代码地址** `0xffffffff8394d2a8`，
  SP `0xffffffffeea5c9e8`（与正常内核栈 `0xffffffff961f...` 不同）。
- GDB 断点在 `lockdep_hardirqs_off` 命中，调用者 `irqentry_enter_from_kernel_mode`
  (`0xffffffff8396655e`，`call` 从 `0xffffffff83966559`)，此时 rdi=0、栈健康。
- 结论：不是函数自身截断（GDB 单步所有指令正常），而是**进入该函数前的某处
  破坏了栈/RBP**，或 KASLR 重定位后运行时字节与 vmlinux 文件不一致（QEMU 报的
  RIP 落在指令中途）。属早期启动的 c6c 代码生成/重定位问题，需继续定位。
- 注意：全树 vmlinux 里有**多份同名函数副本**（lockdep_hardirqs_off 至少 4 处），
  是 DCE 兜底按名字跨 TU 标记所致，可能是隐患。
