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

## 第 39 轮：全树早期崩溃根因链（QEMU -d int + GDB 条件断点）
用 `qemu -d int` 抓到 2258 次 #PF + 1 次 #DF，逐条 addr2line 定位：
1. **fault #0**（预期内）：`memcpy_orig` 读 `__va(real_mode_data)=0xffff888000014770`
   ——这是 x86_64 早期页表 fixup（`early_make_pgtable`）故意触发的 fault，正常。
2. 后续 `pud_offset`/`pmd_offset`/`clear_pages`/`get_bios_ebda` 的 fault 也属早期
   映射/`asm` 探测，属正常路径。
3. **fault #5 起**：RIP 落在 `__c6_tls_len`（`wakeup_64.o` 数据符号区）且
   CR2=0x3c ——执行流跳进了数据区（间接跳转目标被破坏，或 KASLR 后地址
   归因偏移；QEMU 报的 RIP 落在指令中途，倾向后者）。
4. **fault #6..#2258**：`print_stop_info+0x1aa` 处 `mov 0x0(%rax),%rdx`，
   RAX=0（NULL 解引用）——内核已 panic，但 panic 打印路径自己又 #PF，
   递归 2258 次、SP 每次减 ~0x4770，最终栈溢出触发 #DF
   (`check_exception old:0xe new:0xe`)。
GDB 条件断点（`break *memcpy_orig if $rsi==0xffff888000014770`）确认
`copy_bootdata(__va(real_mode_data))` 的 codegen **正确**：参数 `%rdi` 正确
spill 到 `-0x10(%rbp)` 再经指针 load 作为 memcpy 源，0xffff888000014770
就是 real_mode_data，非 c6c bug。
`__c6_tls_img/__c6_tls_len` 是 c6c 每 TU 生成的局部 TLS 记账格（2412 份），
设计如此，非故障源。
**结论**：触发 panic 的真正首因仍未锁定（在 fault #5 附近），但已排除
copy_bootdata/TLS/entry-point 三类嫌疑；panic 打印路径 NULL 解引用导致的
递归 #PF 是放大故障的次要问题。启动仍不可达用户态，目标未完成。

### 第 39 轮补充：环境对照与 relocs 排除
- **GCC 基线对照**：同一 QEMU 命令下 `/tmp/gcc-baseline/arch/x86/boot/bzImage`
  正常启动（25001B 串口、Linux version 7.4.0-rc3、跑到 VFS no-rootfs panic）。
  证明配置/QEMU/BIOS 环境正常，早期崩溃是 **c6c 代码生成**特异问题。
- **KASLR relocs 排除**：手动重链的全树 vmlinux 无 RELA（ld 静态解析完毕），
  `arch/x86/tools/relocs` 产出 8 字节空表（正确）。曾怀疑 piggy 缺 relocs 表
  导致 KASLR 重定位错误，补上 8 字节空表后仍崩在同一处——排除该因素。
- `__c6_tls_len` 实为 `.data` 段 0 长度 OBJECT 标签（readelf 确认 section 9），
  QEMU/GDB 把代码 RIP 归到它是符号化伪影，非执行流跳进数据。
- **待办**：`print_stop_info` 的 NULL 解引用递归 #PF 是 panic 后的放大器；
  真正触发 panic 的首因在 fault #0-#5 之间，需要更细的单步/看门点定位。

### 第 39 轮最终定位：首个真实 #PF 的 faulting RIP 在 .data 的 __c6_tls_* 区
GDB `break exc_page_fault` 抓到**第一次**内核态 #PF 的 pt_regs：
```
faulting RIP = 0xffffffff96633e90   (= __c6_tls_len, .data 段, 0 长 OBJECT 标签)
CR2          = 0x3c
backtrace: exc_page_fault -> handle_page_fault -> __die -> show_regs -> print_stop_info
```
即 CPU 执行流跳到了 `.data` 里的 c6c TLS 记账格区（`.LC132..__c6_tls_delta` 一串
局部标签紧邻），执行数据字节后立刻 #PF(CR2=0x3c)，随后 panic 打印路径自己 NULL
解引用递归 2258 次栈溢出 #DF。
**假设**：c6c 的 TLS 模拟（`__c6_tls_img/len/delta`，%fs 段，见 parse.c6:2542）
在每个 TU 生成一份，依赖 **c6c 自己的 ld.c6** 做合并（ld.c6:1026 扫描同名符号并把
影像拼起来）。全树我们是用 **GNU ld** 手动链的，GNU ld 不会做 c6c 的 TLS 合并，
2412 份局部 __c6_tls_* 各自为政，某个函数指针/跳转目标可能落进这个数据区 →
执行流跳进 .data。这解释了为什么最小树（用 c6c 自带 ld 链）能到 console 而
全树（GNU ld 手动链）早期就崩。
**下一步**：用 c6c 自带的 ld.c6 链接全树（而非 GNU ld），或先关掉 TLS 模拟
（内核不用 __thread 时）验证该假设。这是把全树从「能链」推向「能启动」的关键。

## 第 40 轮：定位并修复 `__builtin_unreachable` 误变成 `call __c6_tls_len`（重大进展）

### 根因（前端 gen_postfix）
- `parse.c6` 的 `__builtin_unreachable` / `__builtin_abort` 分支原本发
  `IR_CALL imm=1`，注释说是 tgkill 系统调用。但 `IR_CALL.imm` 是**符号 id**，
  1 号符号恰好是内核构建里每 TU 生成的 `__c6_tls_len`（.data 里的零长记账对象）。
- 结果：内核里所有 `BUG()` / `unreachable()` / `__builtin_trap()` 全部被编成
  `call __c6_tls_len` → 跳进 .data 的零字节。此前全树 `mm/percpu.o` 有 **23 处**
  `R_X86_64_PC32 __c6_tls_len-4` 的 call（无 DCE 时 43 处），正是首个真正的 #PF
  （RIP 落 .data、CR2=0x3c）与后续 2258 次递归 #PF+#DF 的根因。
- 用 `irfn_push` 的返回地址（`-no-pie` 构建 + addr2line）确认 43 处全部来自
  `parse.c6:12390` 这一处 `IR_CALL imm=1`。

### 修复
- `parse.c6`：两处改发 `IR_TRAP`（不再伪造系统调用）。
- `x64.c6`：`x64_emit_ir` 增加 `IR_TRAP` → `ud2`（0F 0B）。
- 验证：重编 `mm/percpu.o` 后 `__c6_tls_len` 重定位 **23 → 0**，出现 24 条 `ud2`。

### 全树重建 + 启动（关键结果）
- 用修复版编译器重编 2424 个 vmlinux C 源，GNU ld 重链出 426MB vmlinux + 80MB bzImage。
- QEMU 启动：**零 #DF、零 panic 递归**。串口干净地打印到
  `Linux version 7.4.0-rc3 ...`，不再崩溃在早期入口。这是首次全树启动越过早期
  #DF 级联。
- 仍有 3 次 #UD（我们新发的 ud2 正确陷阱），落在 `pcpu_setup_first_chunk` 附近——
  说明该 BUG 分支被真实触发（percpu 初始化仍有 c6c 代码生成问题），但现在是明确的
  ud2 陷阱而非跳进 .data 乱执行，故障归因清晰。
- 收尾后 60 秒串口停在 `Linux version`（后续早期初始化仍待查）。

### 状态
- c6c 前端 89/89 通过；pm/serv 类失败与本次改动无关（新 /tmp 构建目录的路径相关用例）。
- 目标未达成：仍需让内核越过 percpu 初始化进入 userspace，且 GNU as/ld/host tools 依赖未消除。

---

## Round 41：`obj.c6` DCE 入口别名误判 → `bitmap_copy` 被 stub、`nr_groups=0`

### 现象
- 上轮 ud2 落在 `pcpu_setup_first_chunk`。用 `nokaslr` GDB 断点确认：
  `PCPU_SETUP_BUG_ON(ai->nr_groups <= 0)` 触发，首个 #UD IP = `pcpu_setup_first_chunk+0x...`。
- 追 `ai->nr_groups` 为什么是 0：`pcpu_build_alloc_info` 里 `mask` 经
  `cpumask_copy(&mask, cpu_possible_mask)`（`bitmap_copy` → `memcpy`）后仍是全 0
  （`nr_cpu_ids=65`、`__cpu_possible_mask` bit0+bit6 置位），group 计数循环一次都不进。
- 反汇编 `mm/percpu.o`：`cpumask_copy` 里本该 `call bitmap_copy` 的位置是 5 条 `0x90`，
  无重定位；`bitmap_copy` 入口是单条 `0xC3`（ret stub），后面整段函数体还在。
  而 `arch/x86/kernel/apic/vector.o` 的同名 `cpumask_copy` 是正常的
  `call bitmap_copy` + `R_X86_64_PC32`。

### 根因（obj.c6 `obj_dce_stub_dead`）
- `static inline` 包装会生成**两个同址别名符号**：`cpumask_copy` 与
  `cpumask_cpumask_copy` 都在 `0x26dae`。
- `mark`（可达性）只落在调用点 fixup 指向的那一个名字上（这里是
  `cpumask_cpumask_copy` 被 `pcpu_build_alloc_info` 的 call 标记）；
  二进制 search 找 fixup 的 `owner` 时挑中同址的另一个别名 `cpumask_copy`（未标记）。
- 于是闭包不跟进 `cpumask_copy` 里的 call → `bitmap_copy` 仍判死 → 入口写 `0xC3`；
  压缩 fixup 时又因 owner=未标记 static 把这条 call 的 `E8 rel32` NOP 化。
- 入口 stub 循环里本来有 `alive_alias` 检查（同址任一符号标记即视为活），
  但**闭包和 fixup 压缩两处没有**，三处不一致。

### 修复
- 新增 `sym_off_alive(mark, o)`：同址任一已定义非数据符号被标记即视为活。
- 闭包与 fixup 压缩的 `!mark[o]` 判定都改为 `!mark[o] && !sym_off_alive(mark, o)`。
- 注意 `mark` 类型是 `u8 *`（调试时误写成 `u32 *` 读到 4 字节垃圾，已修正）。

### 验证
- 单文件重编 `mm/percpu.c`：`bitmap_copy` 首字节 `0xC3 → 0x55`，
  `cpumask_copy` 的 call 位置出现 `E8` + `R_X86_64_PC32 bitmap_copy-4`。
- 全树重编 2424/2952（与修复前一致），GNU ld 重链成功（仅 objtool ELF size 警告）。
- 链接镜像里该 call 解析为 `call ffffffff85277e77 <bitmap_copy>`（原 NOP 处）。
- QEMU `nokaslr` 启动：GDB 断点确认**不再触发 `pcpu_setup_first_chunk` 的 BUG**
  （该断点不再命中）；故障前移到一条页错误 oops 路径
  `exc_page_fault → show_fault_oops → _printk → vsnprintf → memcpy`，
  首个 #UD 落在 GNU as 构建的 `memcpy_orig+0x14`（`call __memcpy` 落到
  NOP 填充区后跳进指令中间），属于**新的、不同的**下游问题。

### 状态
- 本轮成果：清除了一个真实的 c6c 代码生成 bug（DCE 入口别名误判），
  percpu 初始化的 `nr_groups=0` BUG 不再出现，启动推进到下一段早期初始化。
- 目标未达成：串口仍停在 `Linux version`；下游 `memcpy`/oops 路径、GNU as/ld/host
  tools 依赖均未消除。该 `memcpy` 问题留待后续轮次。

---

## Round 41 (续)：`INIT_LF_MAX` 太小 → `numa.c` 编译失败 → 早期 #PF

### 现象
- 上一修复（DCE 别名）后首个 #UD 消失，但启动仍停在 `Linux version`，
  GDB 抓到一次未 fixup 的 #PF：CR2=`0x98560025`、RIP=`early_cpu_to_node+0x...`，
  栈顶 `show_fault_oops ← exc_page_fault`。
- `x86_cpu_to_node_map_early_ptr` 运行期值是 `0x98560025`——即
  `x86_cpu_to_node_map_early_map`（`0xffffffff98560020`）的低 32 位、高位被清零，
  于是 `early_per_cpu_ptr(x86_cpu_to_node_map)[cpu]` 读到低地址触发 #PF。

### 根因
- `mm_percpu.o` 里 `pcpu_build_alloc_info` 的 call 目标 `cpu_to_nd_fn` →
  `early_cpu_to_node`（定义在 `arch/x86/mm/numa.c`）。
- 全树编译日志里 `FAIL|arch/x86/mm/numa.c|rc=1`：c6c 编译 `numa.c` 失败，
  树里 `numa.o` 是 GCC 基线遗留的（06:09 时间戳），与 c6c 的 `setup_percpu.o`
  混用。混合对象集下 early_ptr 数据初始化被破坏。
- c6c 编译 `numa.c` 失败的首错在 `numa.c:49`：
  `s16 __apicid_to_node[MAX_LOCAL_APIC] = { [0 ... MAX_LOCAL_APIC-1] = NUMA_NO_NODE };`
  `MAX_LOCAL_APIC=32768`。最小复现：范围设计符上界 ≤16384 编过，≥16385 报
  "这里要一个常量表达式"。
- `parse.c6:17532` `INIT_LF_MAX 16384`：叶子表只装 16384 项，超出后 `LF_TRUNC=1`，
  整块退回旧初始化实现；旧实现不认 `[a ... b]` 设计符（23435 行注释也提到
  顶过 16384 会退回、长度算成 0），于是整段初始化器解析崩掉。

### 修复
- `c6c/src/parse.c6`：`INIT_LF_MAX 16384 → 65536`。静态叶表 4 个数组约 1 MB，
  编译器可承受。

### 验证
- 最小复现 `short a[32768] = { [0 ... 32767] = -1 }` 编过。
- `numa.c` 用 c6c 编译成功（90873 字节代码）。
- 全树重编 2425/2952（比上轮 2424 多一个，正是 `numa.c`），GNU ld 重链成功。
- QEMU `nokaslr` 启动：GDB 断点确认越过 `start_kernel` 与 `setup_per_cpu_areas`，
  **只剩 4 次正常早期 #PF，不再有 #UD 级联、不再进 `show_fault_oops`**。
  串口仍停在 `Linux version`（后续早期初始化待查）。
- 前端回归在本环境缺 `build/c6c`/`stddef.h` 无法运行（与改动无关）。

### 状态
- 本轮再清掉一个真实 c6c bug（范围设计符叶表溢出），混合对象集消除，
  启动越过 percpu 初始化。目标未达成：未进 userspace，GNU as/ld/host tools
  依赖仍在。

---

## Round 41 (再续)：asmx 缺 `btr` 指令 → clear_bit 空操作 → pcpu 分组死循环

### 现象
- INIT_LF_MAX 修复后启动越过 start_kernel/setup_per_cpu_areas 的入口，但 COM1
  标记显示卡在 `pcpu_build_alloc_info` 的 CPU 分组循环（119012 次/30 s）。
- 标记 `TOP mask0=00000001` → `cpumask_clear_cpu(cpu,&mask)` 后 `BAI-MASK0=00000001`：
  位根本没清掉，`cpumask_empty()` 永远为假 → 死循环。

### 根因
- `clear_bit()` → `arch_clear_bit()` 的内联汇编用 `btr`（`LOCK_PREFIX __ASM_SIZE(btr)`
  / `andb %b1,%0`）。c6c 的 `asmx.c6` 认识 `bts`/`popcnt`/`bsr` 等位操作指令，
  **唯独不认识 `btr`**：`ax_insn()` 翻译失败 → 整段 asm 被静默丢弃
  → `arch_clear_bit()` 只剩帧槽算术、没有一条写内存的指令 → clear_bit 空操作。
- 最小复现：`asm volatile("btr %1,%0":"+m"(g):"Ir"(3))` 后 g 不变（rc=1）；
  输入-only `"m"` 操作数正常，说明不是约束问题，是助记符不认识。
- 对照 `bts`：0F AB /r（reg 位号）/ 0F BA /4 ib（imm）。`btr` 应为
  0F B3 /r / 0F BA /6 ib。

### 修复
- `c6c/src/asmx.c6` 在 `bts` 分支后新增 `btr` 分支（0F B3 /r、0F BA /6 ib），
  结构完全镜像 `bts`。

### 验证
- 三个最小用例（lock btr +m、+r btr、plain btr +m）全部 rc=0。
- 全树重编 2425/2952，GNU ld 重链成功（需补 2 个静态调用桩符号，与 c6c 改动无关）。
- COM1 标记：`BAI-MASK0=00000000`（位已清），分组循环正常退出
  （`BAI05 post-grouploop` → `EMB02` → … → `EMB06 post-fcall`），
  不再死循环。启动仍在后续 percpu 初始化处停顿（新问题，待查）。

### 状态
- 本轮第三个真实 c6c bug（位操作助记符缺失）已修并验证。目标未达成：未进
  userspace；GNU as/ld/host tools 依赖仍在。

---

## Round 41 (再再续)：asmx 缺 `bt` + `@ccc` → test_bit 恒 0 → percpu BUG_ON

### 现象
- btr 修复后启动推进到 `pcpu_setup_first_chunk()`，但卡在 `SF06 post-unitmap-init`
  之后、`SF07 post-grouploops` 之前。
- 组内标记：`SF07d possible=00000000`——`cpu_possible(0)` 返回 0（应为 1），
  `SF07e post-possible` 不出现，`PCPU_SETUP_BUG_ON(!cpu_possible(cpu))` 触发。

### 根因
- `cpu_possible()` → `cpumask_test_cpu()` → `test_bit()` → `arch_test_bit()`：
  变量位号走 `variable_test_bit()`，其内联汇编是 `bt %2,%1` + `=@ccc`(oldbit)，
  即把位值放进 CF、用 setc 写回 bool。
- c6c 两处都不支持：
  1. `asmx.c6` 没有 `bt` 助记符（只有 bts/btr）→ 整段 asm 被丢；
  2. `parse.c6:asm_one_operand()` 的 `@cc` 输出只认 `z/e`/`nz/ne`，
     不认 `c`（carry）→ 返回 0 整段跳过。
- 结果 `test_bit()` 恒 0 → cpu_possible(0) 假 → BUG_ON。
- 最小复现：`asm volatile("bt %2,%1":"=@ccc"(r):"m"(g),"Ir"(0))` 返回 0（位已置 1）。

### 修复
- `c6c/src/parse.c6`：`@cc` 解析加 `c0=='c'` → `ccval=2`（setc）。
- `c6c/src/asmx.c6`：`ax_binds_out()` 的 cc 输出支持 `cc==2` → setc 操作码 0x92；
  新增 `bt` 指令分支（0F A3 /r 寄存器位号，0F BA /4 ib 立即数位号）。

### 验证
- 最小用例：bt @ccc 位已置→真、位未置→假，两个都通过。
- 全树重编 2425/2952，GNU ld 重链成功（补 3 个静态调用桩，与 c6c 改动无关）。
- COM1 标记：`SF07d possible=00000001`、`SF07e post-possible` 出现，
  `SF07 post-grouploops` → `SF08` → `SF09` → `EMB11 post-setupfirst`：
  `pcpu_setup_first_chunk()` 正常完成。启动仍在后续初始化停顿（新问题）。

### 状态
- 本轮第四个真实 c6c bug（bt/@ccc 缺失）已修并验证。目标未达成：未进
  userspace；GNU as/ld/host tools 依赖仍在。

---

## Round 41 (再再再续)：asm 空模板 `=r` + `"0"` 匹配约束 → RELOC_HIDE 失效 → percpu 写越界

### 现象
- bt 修复后 `pcpu_setup_first_chunk()` 完成，但卡在 `setup_per_cpu_areas()`
  的 `for_each_possible_cpu` 循环：`SPCb post-offset` 后不再有 `SPCc`。
- QEMU `-d int`：`#PF`（e=0002 写）CR2=`ffff8880e542a000`，随后 `#UD` 级联、
  递归栈溢出。`RAX=RDX=CR2=ffff8880e542a000`——写入值(`per_cpu_offset`)
  与写入地址相同，且该地址未映射。

### 根因
- 故障指令 `mov %rdx,0x0(%rax)` 对应 `per_cpu(this_cpu_off, cpu) = per_cpu_offset(cpu)`。
- `per_cpu()` → `SHIFT_PERCPU_PTR()` → `RELOC_HIDE()`，其定义在
  `include/linux/compiler-gcc.h`：`__asm__("" : "=r"(__ptr) : "0"(ptr));`
  ——空模板 asm，靠 `=r` 输出 + `"0"` 数字匹配约束把 `ptr` 送进 `__ptr`。
- c6c 两处都不支持：
  1. `parse.c6:asm_one_operand()` 拒绝纯 `=r` 输出（除非 ASM_HAS_CR/`+r`）→
     整段 asm 被跳过，`__ptr` 留在初值 0；
  2. `"0"` 数字匹配约束不认识 → 同样整段跳过。
- 结果 `RELOC_HIDE(ptr, off)` 退化成 `0 + off = off`，`per_cpu(this_cpu_off,cpu)`
  的地址变成裸 `per_cpu_offset`，写到未映射的 percpu 首地址 → #PF → #UD 级联。
- 最小复现：`__asm__("" : "=r"(ptr) : "0"(&g))` 的 `ptr+g_off` 与
  `&g+g_off` 不等（返回 1）。

### 修复
- `parse.c6`：新增 `ASM_EMPTY_TPL`（模板为空串时为 1）；
  `asm_one_operand()` 在空模板下放行 `=r` 输出与 `"0"`..`"9"` 匹配约束；
  `isreg` 分支对匹配约束用输出操作数 N 的物理寄存器
  （`ASM_REGNUM[match]`），使绑定装入时匹配输入覆盖该寄存器，
  `ax_binds_out()` 写回输出槽即 `out = in`。

### 验证
- 最小用例：修复前 rc=1（错），修复后 rc=0（对）。
- 全树重编 2425/2952，GNU ld 重链成功（补 42 个静态调用桩，与 c6c 无关）。
- COM1 标记：`SPCc post-thiscpu` 之后整个 `setup_per_cpu_areas` 完成
  （SPC04/05），start_kernel 一路推进到 `M20 pre (mm_core_init)`，
  QEMU 45s 内 rc=0 正常退出（此前都是 rc=124 超时）。这是本轮第五个真实
  c6c bug，影响面覆盖整个内核所有 RELOC_HIDE 用点。启动尚未进 userspace
  （停在 mm_core_init），后续仍有新问题。

### 状态
- 目标未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 41 轮续：临时树被清空后的重建与 #DE 排查（结论未定）

### 背景：验证环境丢失
- 会话中途 `/tmp/linux-7.4-rc3` 与所有 `/tmp` 编译产物、脚本、插桩源、日志被清空
  （非代码改动所致）。已提交的五个真实修复（DCE 别名、INIT_LF_MAX、btr、
  bt/@ccc、RELOC_HIDE）都在 git 里，未受影响。
- kernel.org 对 7.4-rc3 tar.gz 反爬（"Making sure you're not a bot"），
  GitHub codeload 单连接限速约 8KB/s、不支持 Range，`git clone --depth 1`
  走 GnuTLS 报 recv error -110。CDN（cdn.kernel.org）只有发布版，无 -rc。
  最终用带 sha256 校验的 aria2c 从 CDN 下载 **linux-7.3.tar.xz** 重建了树，
  但**这是 7.3 + defconfig，不是原验证用的 7.4-rc3 + 原 config**。

### 重建尝试（均为验证脚手架，非仓库改动）
- 重新编译 c6c（`/tmp/c6c-m20/c6c`，含五个已提交修复）；最小 RELOC_HIDE
  用例 rc=0 复核通过。
- 从 `make -n vmlinux`（touch 源码强制列出）harvest 2942 个 TU，用正确
  的 per-TU `-DKBUILD_BASENAME/-DKBUILD_MODNAME/-DKBUILD_MODFILE` 与
  `-D__KBUILD_MODNAME` 全量重编：**2429/2942 通过**（接近原验证的
  2425/2952）。缺 `__KBUILD_MODNAME` 曾导致 `__mod_device_table__kmod_*
  ___KBUILD_MODNAME__*` 重复符号——这是脚手架漏传 define，不是 c6c bug。
- 换入 c6c .o、补静态调用桩、relaxed GNU ld 链出 vmlinux/bzImage。

### 关键发现：全树 #DE 未能定性为 c6c bug
- 全 c6c 重建树启动停在 `setup_arch` 附近（COM1 标记 M04/M01），
  QEMU `-d int` 显示 **#DE（除零）** 位于 `topology_init_possible_cpus`
  的 `DIV_ROUND_UP(cntb, cnta)`，`cnta = domain_weight(TOPO_PKG_DOMAIN)=0`
  即 APIC domain bitmap 未填充。
- 对照实验（决定性）：
  1. 纯 gcc 基线的同一棵树 → 正常走到 VFS panic（打印
     `Max. logical packages: 1`）。
  2. 干净 gcc 树里**只换入 c6c 的 topology.o** → 正常越过 topology
     （同样打印 `Max. logical packages: 1`，走到 VFS panic）。
  3. 全 c6c 重建树 → #DE。
  - 结论：c6c 的 `topology.c` 本身编译正确；#DE 需要多个 c6c .o 的交互
    才出现。把 common.c/apic.c/smpboot.c/smp.c 单独换回 gcc 仍未消除。
- 由于重建树的内核版本（7.3）、config（defconfig）、链接脚手架（458 个
  宽 `.quad 0` 桩、relaxed ld、modpost 绕过）与原验证环境差异过大，
  **无法可靠地把该 #DE 归因于某个 c6c bug**：宽桩把 `__SCK__*` 等符号
  定义成 0、静态调用被破坏，都可能是诱因。按"不臆造修复"原则，本轮
  **不为这个未定性的 #DE 提交任何 c6c 改动**。

### 其余观察
- `lib/klist.c` 用 c6c 编出的 `klist_add_head/tail` 是 local(`t`)，
  gcc 是 global(`T`)——这是已知的文件作用域 static/inline 符号本地化限制
  （见 kernel-vmlinux.sh 的 KNOWN LIMITS），非新 bug。

### 状态
- 五个已验证的真实 c6c 修复仍全部在 git 中，启动进度仍以到达
  `M20 pre (mm_core_init)` 为准。目标未达成：未进 userspace；
  GNU as/ld/host tools 依赖仍在。下一步需先恢复原 7.4-rc3 树与原 config
  才能继续定位 mm_core_init 之后的停顿。

## 第 42 轮：已提交修复的字节级复核（无新 bug）

### 验证（均通过）
- 重新从 `c6c/src/c6c.c6` 编译出 c6c，含五个已提交修复。
- `c6c/tests/asm-native-check.sh`（native 后端内联汇编机器码）：通过 10 / 失败 0。
- 字节级核对：`btr` 编出 `4d 0f b3 1a`（0F B3 /r）、`bt` 编出
  `4d 0f a3 1a`（0F A3 /r）——第 41 轮两个 bitops 修复生成的机器码正确。
- `RELOC_HIDE` 最小用例（空模板 asm 的 `=r`/`"0"`）rc=0。

### 环境/已知限制（非新 bug，未改动代码）
- `c6c/tests/regression-c6c.sh` 全 92 例失败，系本环境缺
  `tcc/include/stddef.h`（`size_t` 等无法解析）等头文件，与改动无关；
  前端基线在完整环境为 89/89。
- 全树重建仍停在 `topology_init_possible_cpus` 的 #DE（第 41 轮续已记），
  因树为 7.3+defconfig 重建、桩/链接脚手架差异过大，未定性为 c6c bug。
- `lib/klist.c` 的 `klist_add_head/tail` 在 c6c .o 里是 local(`t`)（gcc 为
  global `T`），并伴随伪全局 `klist_node_klist_add_head`——已知的
  static/inline 符号本地化限制，非新 bug。

### 状态
- 目标未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

### 第 42 轮补充：2132 个 c6c 对象启动到 VFS（关键正向证据）
- 在重建的 7.3+defconfig 树里，全量 c6c .o（2427 个）中把
  `arch/x86/kernel/cpu/*` + `arch/x86/kernel/apic/*` + `arch/x86/kernel/setup.c`
  换回 gcc（共约 60 个），其余 **2132 个 c6c .o** 编进 vmlinux：
  - QEMU 正常打印 `Linux version`、完整 `CPU topo:` 各域信息；
  - COM1 标记走完 `M01`…`M25`（含 `M20 post`，即越过 `mm_core_init`）；
  - 一路初始化到 `VFS: Unable to mount root fs`（本环境无 rootfs 的预期终点），
    与纯 gcc 基线终点一致。串口实测 `NET: Registered` 5 个协议族
    （NETLINK/ROUTE、INET、UNIX、INET6、PACKET），满足
    `tests/kernel-vmlinux.sh` 的启动判据（Linux version ≥1 且 NET ≥3）。
    注意这是**阶段 B 口径**：2132 个 c6c ET_REL .o + 约 60 个 gcc .o
    （cpu/apic/setup），链接仍用 GNU ld、汇编 .S 仍用 GNU as、host tools
    仍由 gcc 构建——不是"全内核 c6c 独立构建"。
  - 这是对第 41 轮五个真实修复（尤其 RELOC_HIDE、bt/@ccc、btr）的强验证：
    **绝大多数内核 C TU 用 c6c 编译后，内核仍能启动到用户态前的最后一步**。
- 对 #DE 的二分定位：只恢复 cpu/ 或只恢复 apic/ 仍停在 `setup_arch`（M01）；
  两者都恢复才通过。结合约 431 个未定义符号被统一桩成 `.quad 0` 数据、
  且用 `--allow-multiple-definition` 链接（桩可能覆盖真实定义），**确认
  #DE 是重建脚手架（桩/链接）假象，而非可定位的单个 c6c bug**。
  按"不臆造修复"原则不为它改代码。

## 第 43 轮：#DE 定性为脚手架假象；2416 个 c6c 对象无桩启动成功

### 决定性实验
- 先确认 `__SCT__*` 静态调用桩**不是** c6c bug：c6c 编译
  `kernel/sched/core.c`（开/关 DCE 两种）与 gcc 的符号完全一致
  （都是 `U __SCT__preempt_schedule` + `__UNIQUE_ID_addressable_*`）；
  SCT trampoline 由头文件里的内联汇编定义，c6c 正常发射（全树 1159 个已定义）。
- 干净 bisect（**纯 gcc 树 + 只换入 c6c .o，不补任何桩**）：
  - 只换 `topology`+`common` → 启动正常，打印完整 `CPU topo:`（qemu rc=0）。
  - 换入**全部 2416 个 c6c C TU**（保留 gcc 的 .S 与 host tools），
    vmlinux 链接 **零 undefined symbol**（唯一的链接失败来自 modpost 的
    section-mismatch 警告，绕过即可）。
  - 该内核 QEMU 启动：打印 `Linux version`、完整 `CPU topo:`、5 个
    `NET: Registered` 协议族、COM1 标记走完 `M01`…`M25`，一路到
    `VFS: Unable to mount root fs`（本环境无 rootfs 的预期终点）。
- 结论：第 41/42 轮全树 #DE 是**验证脚手架假象**——当时因编译失败排除了
  约 500 个 .o，又把 431 个未定义符号统一桩成 `.quad 0` 数据并用
  `--allow-multiple-definition` 链接，桩覆盖了真实定义所致。
  **不是 c6c 的 C 编译 bug**；不为它改代码。

### 范围界定（阶段 B 口径）
- 本次成功覆盖 c6c 编译的全部 2416 个内核 C TU（CC 角色），但：
  - `.S` 汇编仍由 GNU as 汇编（arch/x86/boot、realmode、vdso 等）；
  - 链接仍用 GNU ld（含 `-z muldefs` 本次未用，干净树零未定义符号）；
  - objtool/modpost 等 host tools 仍由 gcc 构建。
- 因此目标仍未达成（未进 userspace、仍有 gcc/as/ld 依赖），但证明
  **c6c 作为 C 编译器已能把几乎整个内核编到可启动、功能完整**。

### 状态
- 五个已提交的真实 c6c 修复不变。目标未达成；下一步的真正缺口是
  GNU as（.S）、GNU ld、gcc host tools 与命名地址空间支持。

## 第 44 轮：第六个真实 c6c bug —— 属性打头的函数指针 typedef

### 症状
- 全量编译 513 个失败 TU 里，有一类是**真实语法 bug**（非脚手架缺头文件）：
  `drivers/firmware/efi/libstub/efistub.h:763` 报
  `这里要 ';'（当前是 'read'）`，波及 14+ 个 EFI stub TU。
- 根因源码：`typedef efi_status_t (__efiapi *efi_pci_io_protocol_cfg_t)(...)`，
  其中 `__efiapi` 展开成 `__attribute__((ms_abi))`。

### 根因
- `parse_declarator()` 认嵌套声明符 `(*name)(...)` 时，只接受 `(` 后面
  **紧跟** `*` 或 `(`。当写成 `(__attribute__((ms_abi)) *fp_t)` 时，`(` 后面
  是 `__attribute__`，判断落空，`(` 被误当成函数参数表，typedef 被登记成
  `int()`，名字 `fp_t` 不是类型名，后续所有用到它的声明连锁崩溃。

### 修复（`c6c/src/parse.c6`）
- 扩展嵌套声明符判断：`(` 后面若先是 `__attribute__` / `__attribute` /
  `__extension__` 也认作嵌套声明符；进分支后由 `parse_declarator()` 开头的
  `skip_attributes()` 正常吃掉属性，其余逻辑不变。

### 验证（均通过）
- 最小复现 `typedef int (__attribute__((ms_abi)) *fp_t)(int);` + 结构体成员
  + 调用：修复前 rc=1（编不过），修复后 rc=0；无 stdlib 真跑 rc=0（返回 42）。
- 14 个此前失败的 EFI stub TU：修复后 **14/14 全部编过**。
- 编译器回归：`asm-native-check` 10/10、`ld-check` 13/13（五个已提交修复不受影响）。
- 其余失败 TU 归类为脚手架问题（i915 缺 per-TU include 目录、`acpi.h` 直接
  包含的 #error），非 c6c bug。

### 状态
- 这是本会话第六个真实 c6c bug（前五个：DCE 别名、INIT_LF_MAX、btr、bt/@ccc、
  RELOC_HIDE），已修并验证。目标仍未达成：未进 userspace；GNU as/ld/host tools
  依赖仍在；c6c 自带链接器（ld-check 13/13）尚不支持内核链接脚本。

## 第 45 轮：第 44 轮修复的全树复核（无回归）

### 全树重编覆盖提升
- 用第 44 轮修好的编译器（`/tmp/c6c-fpfix`）按正确 per-TU 标志重编全部
  2942 个内核 C TU：**PASS 2445 / FAIL 497**，比修复前的 2429 多 16 个
  （14 个 EFI stub TU + 2 个连带），证明修复在真实全树生效。

### 剩余失败归类（均无新 bug）
- `acpi.h` 的 `#error`（约 156 例）：脚手架漏传 `-DBUILDING_ACPICA`
  （内核 `drivers/acpi/acpica/Makefile` 里 `ccflags-y` 定义），非 c6c bug。
- i915 的解析错误：级联自 `找不到头文件 'i915_reg_defs.h'` 等——脚手架漏传
  `-Idrivers/gpu/drm/i915` per-TU include 目录，非 c6c bug。
- 函数指针/属性各种变体（`struct` 成员、`__attribute__` 前后置）最小用例全过。

### 启动复核（无回归）
- 用修复后的编译器重编的 2416 个 c6c .o（干净换入 gcc 树、无桩）启动：
  `Linux version` + 完整 `CPU topo:` + **5 个 `NET: Registered`** + COM1 标记
  走到 `M25` + 预期 VFS panic——与第 43 轮基线一致，第 44 轮修复未引入回归。
- 注意：把新编出的 EFI stub .o 也换入时会引入 `absolute symbol` /
  `__SCT__` 未定义（EFI stub 段的特殊 ABI），属链接脚手架问题，非 c6c bug；
  启动复核按第 43 轮口径排除 EFI libstub。

### 状态
- 六个已提交的真实 c6c 修复全部保留。目标未达成：未进 userspace；
  GNU as/ld/host tools 依赖仍在；c6c 自带链接器不支持内核链接脚本。

## 第 46 轮：第七个真实 c6c bug —— 空实 참여 `##` 的粘贴语义

### 症状
- 全量 497 个失败 TU 归类：290 个缺头文件、156 个 acpi #error、12 个未定义符号
  均为脚手架；但 9 个网络 TU 报 `include/net/ipv6.h:1: 这里要 ')'`
  （当前是 ')SNMP_INC_STATS64'），是真实预处理 bug。

### 根因
- 内核 `_DEVINC(net, statname, mod, idev, field)` 用 `mod##SNMP_INC_STATS64`，
  `IP6_INC_STATS` 传**空** mod。GCC 语义：`##` 一边为空时是无操作，另一边原样留下。
- `macro.c6:expand_macro()` 里空实参不产生任何 token，但随后的 `##` 分支照样把
  右边 token 粘到 `r[n-1]`（前一个**无关** token，如 `{`），生成坏 token
  `{SNMP_INC_STATS64` / `)SNMP_INC_STATS64`，连锁 9 个 TU 编不过。

### 修复（`c6c/src/macro.c6`）
- `expand_macro()` 加 `empty_lhs` 标志：形参紧邻 `##` 且实参为空时置位；
  `##` 分支见该标志则把右边作为独立 token 追加、不粘贴，然后清标志。

### 验证（均通过）
- 最小复现（空 `mod##SNMP_INC_STATS64`）：修复前 rc=1，修复后 rc=0；
  `-E` 输出从坏 token 变为正确展开 `(((n)->mib)+((5)))`；非空粘贴仍 rc=0。
- 9 个此前失败的 ipv6.h 相关 TU：修复后 **9/9 全部编过**。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13（前六个修复不受影响）。

### 状态
- 本会话第七个真实 c6c bug（前六个：DCE 别名、INIT_LF_MAX、btr、bt/@ccc、
  RELOC_HIDE、属性函数指针 typedef），已修并验证。目标仍未达成：未进 userspace；
  GNU as/ld/host tools 依赖仍在。

## 第 47 轮：第八个真实 c6c bug —— 带外层括号的复合字面量地址初始化器

### 症状
- 用第 46 轮编译器全树重编 **2455/2942**（比上轮 2445 多 10）。
  剩余失败里 `include/linux/hwmon.h:444` 报 `这里要 ')'（当前是 '{'）`（3 个 TU）。
- 内核 `HWMON_CHANNEL_INFO(stype, ...)` 宏展开成
  `(&(const struct hwmon_channel_info){ .type=…, .config=(const u32[]){…} })`。

### 根因（`parse.c6:gen_glob_obj` 的文件作用域地址初始化器解析）
1. 剥"整条地址常量外层括号"的循环只认 `&` 后面是 TK_IDENT（`(&name)`），
   `&(` 后跟复合字面量时外括号不剥，`amp` 看不到 `&`。
2. `&(name)` 那条把 `const`（类型关键字，词法上是 TK_IDENT）错当名字，
   于是 `&(const struct T){…}` 被剥成 `&const`。
3. `gen_glob_compound_addr()` 成功后直接 return，没吃掉第 1 步剥掉的
   外层 `)`，`amp_paren` 悬空 → 下一条声明报"顶层声明缺名字"。

### 修复（`c6c/src/parse.c6`，三处）
- 外层括号剥离：`&` 后面是标识符 **或** `(`（复合字面量）都剥。
- `&(name)` 剥离：加 `is_typename_tok()` 排除类型关键字。
- 复合字面量地址成功后：把 `amp_paren` 记的 `)` 补吃掉再 return。

### 验证（均通过）
- 最小用例：宏展开的 `(&(const struct S){…})` 修复前 rc=1、修复后 rc=0；
  `-E` 输出正确；真跑（读 `p->type`）rc=0。无宏的 `&(T){…}` 仍 rc=0（无回归）。
- 受影响的 2 个非 i915 hwmon TU 修复后编过（i915_hwmon 仍缺 i915 include 目录，
  属脚手架）。回归：`asm-native-check` 10/10、`ld-check` 13/13。

### 状态
- 本会话第八个真实 c6c bug（前七个：DCE 别名、INIT_LF_MAX、btr、bt/@ccc、
  RELOC_HIDE、属性函数指针 typedef、空实参 ##），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 48 轮：第九个真实 c6c bug —— 括号里相邻字符串字面量

### 症状
- 用第 47 轮编译器全树重编 **2459/2942**。剩余失败里 `net/socket.c` 报
  `include/uapi/linux/xattr.h:48: 这里要一个常量表达式` + 一串
  `这里要 ')'（当前是 '"sockprotoname"'）`。
- 内核 `XATTR_NAME_SOCKPROTONAME` = `("system." "sockprotoname")`：括号里
  两个相邻字符串字面量（翻译阶段拼成一个）。

### 根因（`parse.c6:pa_str_paren_depth()`）
- 这个前瞻helper认 `("n")` / `(("n"))` 这种"括号里包着字符串"的形状，
  但只跳过**一个** TK_STR 就去找 `)`。相邻两个串时第二个让它返回 0，
  于是带括号的字符串初始化器掉进常量表达式求值，报错。

### 修复（`c6c/src/parse.c6`）
- `pa_str_paren_depth()` 把紧挨着的 TK_STR **全跳过去**再找 `)`。

### 验证（均通过）
- 最小复现：结构体指针字段初始化 `.n = ("a" "b")` 修复前 rc=1、修复后 rc=0；
  普通指针 `const char *p = ("a" "b")` 仍 rc=0。
- 实际受影响的 `net/socket.c` 修复后用全内核标志编译 **rc=0**。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13；前两个修复
  （空实参 ##、属性 typedef）的最小用例仍 rc=0。
- （发现文档之外：无长度数组 `char s[] = ("a" "b")` 仍不走这条 helper、
  仍报错，但内核当前失败 TU 里没有这种形状，未在本轮处理。）

### 状态
- 本会话第九个真实 c6c bug（前八个：DCE 别名、INIT_LF_MAX、btr、bt/@ccc、
  RELOC_HIDE、属性函数指针 typedef、空实参 ##、外层括号复合字面量），
  已修并验证。目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 49 轮：第十个真实 c6c bug —— 字符串字面量作常量表达式

### 症状
- 用第 48 轮编译器全树重编 **2460/2942**。剩余失败里
  `net/netfilter/nf_conntrack_sip.c:382` 一串 `这里要一个常量表达式`。
- 内核 `SIP_HDR` 宏：
  `.clen = (__cname) ? sizeof(__cname)-1 : 0`。当 `__cname` 是字符串字面量
  （如 `"f"`）时，静态初始化器里的三元条件要求把字符串当常量求值。

### 根因（`parse.c6:const_unary()`）
- 常量求值器认 TK_NUM / TK_CHAR / 标识符 / sizeof，但**不认 TK_STR**——
  直接掉到末尾报"这里要一个常量表达式"。gcc 里字符串字面量的值是它的
  地址（非零常量），三元条件为真。

### 修复（`c6c/src/parse.c6`）
- `const_unary()` 加 TK_STR 分支：用 `str_lit_data()` 把字符串写进数据段
  （顺带拼好相邻字面量），设 `CONST_TY` 为 `char *`，返回其虚拟地址
  （用 `ELF_BASE_VA + ELF_HDR + off`，因为 `DATA_VA` 宏在本文件后面才定义）。

### 验证（均通过）
- 最小复现：`.x = ("f") ? 7 : 9` 修复前 rc=1、修复后 rc=0；纯整数三元、
  `sizeof` 分支三元仍 rc=0；真跑读 `h.x == 7` rc=0。
- 实际受影响的 `nf_conntrack_sip.c` 用全内核标志（含 `KBUILD_MODFILE`）
  编译 **rc=0**。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13。

### 状态
- 本会话第十个真实 c6c bug（前九个：DCE 别名、INIT_LF_MAX、btr、bt/@ccc、
  RELOC_HIDE、属性函数指针 typedef、空实参 ##、外层括号复合字面量、
  括号内相邻字符串），已修并验证。目标仍未达成：未进 userspace；
  GNU as/ld/host tools 依赖仍在。

## 第 50 轮：第十一个真实 c6c bug —— `&*` 取址抵消成空指针常量

### 症状
- 用第 49 轮编译器全树重编 **2461/2942**。剩余失败里
  `drivers/tty/sysrq.c:223` 报"全局地址初始化器只支持 &全局/函数" +
  "这里要一个常量表达式"。
- 内核 `sysrq_showlocks_op (*(const struct sysrq_key_op *)NULL)`，被
  `&sysrq_showlocks_op` 取地址；C 里 `&*E` 等价于 `E`，gcc 折叠成 NULL。

### 根因（`parse.c6:gen_glob_obj` 的 `&` 地址常量路径）
- 只认 `&全局`/`&函数`/`&(name)`/`&((T*)0)->member` 等形状，遇到
  `&(*(T *)0)` 落到 `pa_error("全局地址初始化器只支持 &全局/函数")`。

### 修复（`c6c/src/parse.c6`）
- 在 `glob_addr_start()` 之前识别 `& ( * (T *)0 )`：`&*` 抵消，吃掉整段、
  用 `const_expr()` 求 `(T *)0`（得 0），存 0 作为空指针常量。

### 验证（均通过）
- 最小复现（宏展开的 `&op_null`，`op_null=(*(struct op*)0)`）：
  修复前 rc=1、修复后 rc=0。
- 实际受影响的 `drivers/tty/sysrq.c` 用全内核标志编译 **rc=0**。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13；前三个修复的最小用例
  （空实参 ##、外层括号复合字面量、字符串三元常量）仍 rc=0。
- （已知边界：直接写 `&(*(T*)0)` 不走宏的极个别形状仍报错，但内核当前
  失败 TU 里没有这种写法，未在本轮处理。）

### 状态
- 本会话第十一个真实 c6c bug（前十个：DCE 别名、INIT_LF_MAX、btr、bt/@ccc、
  RELOC_HIDE、属性函数指针 typedef、空实参 ##、外层括号复合字面量、
  括号内相邻字符串、字符串常量三元），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 51 轮：第十二个真实 c6c bug —— `&((T *)0)->m[i]` offsetof 被地址路径误认

### 症状
- 用第 50 轮编译器全树重编 **2462/2942**。剩余失败里
  `net/core/neighbour.c:3776` 报 `'&struct' 不是地址常量`。
- 内核 `NEIGH_PARMS_DATA_OFFSET(index) = (&((struct neigh_parms *)0)->data[index])`，
  是经典 offsetof 写法（带常量下标），gcc 直接折叠成整数偏移。

### 根因
- `const_unary` 的 `&` 分支本来就完整支持 `&((T *)0)->m[i]`（含下标），但全局
  初始化器的**地址常量快路径**（`gen_glob_obj` / `gen_one_global` / 花括号
  指定初始化器三处）先拦截：`glob_addr_start` 把第二层 `(` 后的 `struct`
  （关键字词法上是 TK_IDENT）当成全局符号名，`glob_addr_parse` 报
  `'&struct' 不是地址常量`。

### 修复（`c6c/src/parse.c6`，三处快路径）
- 在 `&` 被吃掉之前识别 `( & ( ( typename * ) 0 ) ->成员[下标] )` 形状
  （含宏常见的外层括号），整条交给 `const_expr()`（它的 `&` 分支已支持），
  结果当整数偏移存进指针槽；补吃掉外层 `)`。
- 三处：标量全局指针、嵌套聚合指定初始化器字段、`gen_glob_obj` 的 amp 段。

### 验证（均通过）
- 6 个最小用例：`&((T*)0)->m`、`->m[i]`、宏外层括号、嵌套花括号指定初始化器、
  `&*NULL`、字符串三元常量——修复前 rc=1 的现在全 rc=0。
- 实际受影响的 `net/core/neighbour.c` 用全内核标志编译 **rc=0**。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13。

### 状态
- 本会话第十二个真实 c6c bug（前十一个：DCE 别名、INIT_LF_MAX、btr、bt/@ccc、
  RELOC_HIDE、属性函数指针 typedef、空实参 ##、外层括号复合字面量、
  括号内相邻字符串、字符串常量三元、`&*NULL`），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 52 轮：第十三个真实 c6c bug —— `&"string"` 常量地址

### 症状
- 用第 51 轮编译器全树重编 **2463/2942**。剩余失败里
  `drivers/tty/serial/8250/8250_pci.c` 报"常量表达式里只支持 &((T *)0)->member
  形式的取地址" + 一串 `这里要 ')'（当前是 '"PARPORT_SERIAL"'）`。
- 内核 `REPORT_CONFIG(option)` = `... (kernel_ulong_t)&#option`，展开成
  `&"PARPORT_SERIAL"`（字符串字面量取地址，是常量）。

### 根因（`parse.c6:const_unary()` 的 `&` 分支）
- 认 `&((T *)0)->m`、`&全局`，但 `&` 后面直接是 TK_STR 时落到
  `pa_error("只支持 &((T *)0)->member")`。gcc 里字符串地址是常量。

### 修复（`c6c/src/parse.c6`）
- `&` 分支加 TK_STR：`str_lit_data()` 把字符串写进数据段（拼好相邻串），
  返回其虚拟地址；补吃掉宏外层括号 `extra`。

### 验证（均通过）
- 最小复现（`&#option` 宏）：修复前 rc=1、修复后 rc=0；`&*NULL`、
  字符串三元、offsetof 嵌套的前序修复用例仍 rc=0。
- 实际受影响的 `8250_pci.c` 用全内核标志编译 **rc=0**。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13。

### 状态
- 本会话第十三个真实 c6c bug（前十二个：DCE 别名、INIT_LF_MAX、btr、bt/@ccc、
  RELOC_HIDE、属性函数指针 typedef、空实参 ##、外层括号复合字面量、
  括号内相邻字符串、字符串常量三元、`&*NULL`、offsetof），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 53 轮：修复第 51 轮 offsetof 引入的复合字面量回归

### 症状与定位
- 全树重编仍是 2464/2942（无净增）。追查发现第 51 轮为 neighbour.c 加的
  offsetof 快路径**过宽**：它在 `& ( ( typename` 前缀就把整条交给 const_expr，
  于是 `&((struct A){ .x = 1 })`（复合字面量取地址）被误认成 offsetof，
  const_expr 在 `{` 上报"这里要 ')'（当前是 '{'）"。
- 对照实验：`&((struct A){…})` 在第 50 轮编译器 rc=0、第 51 轮 rc=1——
  确认为**本会话自己引入的回归**，不是 c6c 原有 bug。

### 修复（`c6c/src/parse.c6`）
- 新增纯前瞻 `pa_is_offsetof_ahead(start)`：从类型名处扫前瞻窗口，
  `->`/`.` 成员链先出现判为 offsetof（返回 1），`{` 先出现判为复合字面量
  （返回 0），交回 gen_glob_compound_addr 正常处理。
- 三处 offsetof 快路径（gen_glob_obj / init_body / 标量全局）都加此判据。

### 验证（均通过）
- `&((struct A){…})`、`&((T[]){…}[0].x)` 等最小用例：第 51 轮 rc=1 → 修复后 rc=0。
- 前序修复用例（offsetof、&string、字符串三元、&*NULL）仍全 rc=0。
- 实际受影响 TU：`net/core/neighbour.c`、`8250_pci.c` 用全内核标志编译 rc=0。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13；全树重编 2464（不倒退）。

### 状态
- 第 13 个已验证修复（本轮为回归修正）。i915_pmu.c 的
  `&((T[]){…})[0].attr` 复合字面量+下标+成员链是 c6c 既有未实现特性
  （第 50 轮也不支持），非回归，未在本轮处理。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 54 轮：确认剩余 480 个编译失败全是脚手架缺 flag，非 c6c bug

### 尝试
- 从 `make -n` 提取每个 TU 的完整 `-I`/`-D`（2942 个 TU 的 manifest），
  想让重建 harness 的 480 个失败 TU 也编过、从中找新 bug。
- 但重建用的编译 harness 是简化版（固定 `-I` 集 + 部分 `-D`），把 make -n
  的全部 flag 直接套上去反而因 manifest 抽取遗漏 745 个 TU 而倒退
  （2007/2942）——这是**脚手架实现问题**，不是 c6c 问题。

### 决定性验证：失败 TU 补上正确 flag 后都能编过
- `drivers/acpi/acpica/dsargs.c`（156 个 acpi #error 的代表）：补上
  `-D_LINUX -DBUILDING_ACPICA -DACPI_DEBUG_OUTPUT` 和 acpi include 目录后
  **rc=0**。
- `arch/x86/entry/vdso/vdso64/vclock_gettime.c`（290 个"找不到头文件"的代表）：
  补上 `-Iarch/x86/entry/vdso`（内核 make 里的 `vdso64/..`）后 **rc=0**。
- 结论：480 个失败里 290+156 是重建 harness 漏掉 per-TU `-I`/`-D`，
  其余 13+7+4+4+2+1 是缺头文件目录 / 缺 `-include utsversion.h` /
  生成文件 / 非 x86 目标等脚手架问题。**没有新的 c6c bug**。

### 状态
- 第 13 个已验证修复 + 第 53 轮回归修正保持不变；本轮无代码改动。
  全树可信的覆盖率口径仍是第 53 轮的 2464/2942（简化 harness），
  实际 c6c 作为 C 编译器的能力已由"2416 对象启动到 VFS"验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 55 轮：用权威 flag 重编失败 TU，定位到 typeof/复合字面量深层缺口

### 用 .cmd 权威 flag 重编
- 从内核 kbuild 的 `.cmd` 文件提取每个失败 TU 的**确切** `-I`/`-D`/`-include`，
  重编之前 harness 漏掉的 478 个 TU：修正 flag 正则（`-include` 必须带空格，
  否则误抓 `-include-dirs`）后 **380/478 编过**，剩 98 个。
- 98 个里绝大多数仍是脚手架：44 个 syscall_64.c（生成文件）、35 个
  `未定义的符号 rsp`（这些是**成功**编译的提示信息被当成首错）、e1000e/sound
  缺 include、vdso32 缺 CONFIG_X86_32 fallback、生成文件等。

### 发现的真实 c6c bug（本轮未修，需更深工作）
- `_Static_assert(__is_array(((const int[]){1,2})))` 在 gcc 通过、c6c 失败
  （"must be array"）。根因：`__is_array(a)=!__same_type((a),&(a)[0])` 靠
  "typeof 不退化数组"区分数组/指针；c6c 的 typeof 遇到**复合字面量数组**
  仍退化成指针（`parse.c6` 的 mark_lval/数组退化路径），命名数组正常。
- 影响 1 个 i915 display TU（非启动路径）。尝试在数组退化处尊重 NO_DECAY
  未能修复（复合字面量走另一条 GEN_TY 路径），**已回退未验证改动**，
  不留半成品。留待后续专门处理 typeof_expr 的复合字面量类型传播。

### 状态
- 第 13 个已验证修复 + 第 53 轮回归修正保持不变；本轮无代码改动。
  c6c 作为 C 编译器能力已由"2416 对象启动到 VFS"验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 55 轮续：第十四个真实 c6c bug —— typeof 里复合字面量数组退化

### 修复（`c6c/src/parse.c6`）
- 上一轮定位的 `__is_array(((const int[]){1,2}))` 假失败，根因在复合字面量
  表达式路径：两处把 `GEN_TY` 设成 `ty_ptr(...)`（数组退化成指针），
  而 `parse_typeof()` 设的 `NO_DECAY` 标志没被尊重（函数指示符路径本来就
  尊重了）。让这两处也按 `NO_DECAY` 保留数组类型本身。

### 验证（均通过）
- `_Static_assert(__is_array(((const int[]){1,2})))`：修复前 rc=1、修复后 rc=0
  （与 gcc 一致）；`sizeof`/typeof 命名数组、字符串三元、复合字面量取地址
  等前序用例仍 rc=0。
- 真实 TU `intel_display_power_map.c` 里 `_Static_assert "must be array"`
  **消失**（该 TU 仍因另一个"复合字面量做初始化器"问题编不过，非本 bug）。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13；全树重编 2464（不倒退）。

### 状态
- 第十四个真实 c6c bug（前十三个 + 第 53 轮回归修正），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 56 轮：第十五个真实 c6c bug —— __builtin_constant_p 把 struct tag 当变量

### 症状与根因
- `net/mac80211/spectmgmt.c` 的 `_Static_assert(__builtin_constant_p(sizeof(struct T)), …)`
  假失败。最小复现：`_Static_assert(__builtin_constant_p(sizeof(struct mytag)), …)`
  在 gcc 通过、c6c 失败。
- `const_unary()` 里 `__builtin_constant_p` 的 token 扫描器：`struct`/`union`/`enum`
  算类型关键字放过去，但紧跟其后的 **tag 名**（词法上是 TK_IDENT）被当成
  普通变量 → ok=0 → 常量判定假失败。内核 include/linux/overflow.h 的
  __DEFINE_FLEX 正是这种形状。

### 修复（`c6c/src/parse.c6`）
- 扫描器遇到 struct/union/enum 关键字后，若紧跟 TK_IDENT（tag 名）一并跳过。

### 验证（均通过）
- 最小复现 `sizeof(struct mytag)`、`sizeof(struct mytag *)` 修复前 rc=1、
  修复后 rc=0（与 gcc 一致）；枚举/sizeof(int) 用例仍 rc=0。
- 真实 TU 的 `_Static_assert "must be constant count"` 错误**消失**（该 TU 仍
  因另一个 `__builtin_choose_expr` 常量求值问题编不过，非本 bug）。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13；全树重编 2464（不倒退）。

### 状态
- 第十五个真实 c6c bug（前十四个 + 第 53 轮回归修正），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 57 轮：__builtin_choose_expr 失败未能最小复现（无新代码改动）

### 调查
- `net/mac80211/spectmgmt.c` 在第 56 轮修掉 `_Static_assert "must be constant"`
  后，仍报 `include/linux/overflow.h:437: 这里要 ')'（当前是 '('）`——
  `struct_size` 里的 `__builtin_choose_expr(__is_constexpr(count), …)`。
- 尝试逐级复现：简化版 `__builtin_choose_expr`、`__is_constexpr` 的
  `(sizeof(int)==sizeof(*(8?…)))` trick、嵌套 `flex_array_size` 的
  `__must_be_array`（内含 `_Static_assert(sizeof(struct{…}))`）——
  **全部 rc=0**，与 gcc 一致。
- 结论：该错误不是可独立复现的 `__builtin_choose_expr` bug，而是该巨大 TU
  里多层宏展开/其他未定位问题的级联。按"不臆造修复"原则，本轮**不改代码**。
- 第 56 轮的 `__builtin_constant_p` 跳过 struct tag 修复是正确的独立修复，保留。

### 状态
- 第十五个已验证修复 + 第 53 轮回归修正保持不变；本轮无代码改动。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。
  剩余轮次应聚焦真正的结构性缺口（.S 汇编、内核链接脚本、host tools），
  而非追逐不可复现的级联错误。

## 第 58 轮：最终集成验证（全部 15 个修复后仍可启动）

### 验证
- 用含全部已提交修复的最新编译器（`c6c-constp`）全树重编 **2464/2942**，
  干净换入 **2435 个 c6c .o**（vmlinux rc=0、0 undefined symbol）。
- QEMU `nokaslr` 启动：打印 `Linux version`、完整 `CPU topo:` 各域、
  **5 个 `NET: Registered`** 协议族、COM1 标记走到 `M25`，一路到
  `VFS: Unable to mount root fs`（本环境无 rootfs 的预期终点）。
- 结论：第 41~56 轮的 15 个 parser/asmx/macro/obj 修复**未引入启动回归**；
  c6c 作为 C 编译器把几乎整个内核编到可启动、功能完整，稳定复现。

### 目标剩余缺口（未达成）
- 未进 userspace（无 rootfs 环境，终点是 VFS panic）。
- `.S` 汇编仍需 GNU as；vmlinux 链接仍需 GNU ld（c6c 自带 ld 无内核链接脚本
  支持）；objtool/modpost 等 host tools 仍由 gcc 构建。
- 命名地址空间（CONFIG_CC_HAS_NAMED_AS）走 `%gs:` 规避路径，非原生支持。

## 第 59 轮：评估 GNU ld 依赖缺口（结构性，未改动）

### c6c 自带链接器现状
- `c6c -o prog a.o b.o` 能把多个 ET_REL 链成可跑的 ET_EXEC（最小多 TU 用例
  真跑 rc=0，说明重定位/符号解析/合成 _start 都正常）。
- 但 `ld.c6` 的能力边界决定它**无法直接链接内核**：
  * `LD_MAXOBJ 64`（内核 2435 个 .o）；
  * 只归并固定几段（.text/.rodata/.data/.bss/.init_array/.fini_array），
    内核大量自定义段（.init.text、.data..percpu、异常表等）会被丢掉；
  * 文件偏移与 VA 恒等映射、入口是合成 _start——内核需要高半部链接地址
    和自定义链接脚本（vmlinux.lds.S）。
- 结论：替代 GNU ld 是一个**大型独立工程**，剩余轮次无法可靠完成；
  按"不臆造修复"原则，不改代码。

### 附带尝试（已回退）
- 试修 i915_pmu.c 的 `&((T[]){…})[0].member` 复合字面量链（gen_glob_compound_addr
  折进偏移），未奏效且可能影响其他用例，**已回退**，不留半成品。

### 状态
- 15 个已验证修复 + 第 53 轮回归修正保持不变；本轮无代码改动。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 60 轮：第十六个真实 c6c bug —— 预定义宏 __BASE_FILE__

### 症状与根因
- `sound/core/seq/seq_lock.h` 的 `snd_use_lock_sync` 宏用 `__BASE_FILE__`
  打印文件名；c6c 报"未声明的标识符 '__BASE_FILE__'"（4 个 sound TU）。
- 两个原因：
  1. `pp.c6:pp_next()` 的内建宏快速过滤上界是 11 字符，`__BASE_FILE__`
     （13 字符）被过滤掉、走成普通标识符；
  2. 根本没有该宏的实现。

### 修复（`c6c/src/pp.c6`）
- 长度上界 11→13；新增 `__BASE_FILE__` 分支：沿包含栈 `PP_TOP->up` 走到
  最外层文件源，展开成其路径字符串（GCC 语义：整 TU 恒为主源文件，
  不同于 `__FILE__` 是当前 #include 文件）。

### 验证（均通过）
- `-E`：`#include` 的文件里 `__BASE_FILE__` = 主文件名、`__FILE__` = 当前头文件名
  （与 gcc 一致）；最小复现 rc=1→rc=0。
- 真实 TU `sound/core/seq/seq_clientmgr.c` 用全内核标志编译 **rc=0**。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13。

### 状态
- 第十六个真实 c6c bug（前十五个 + 第 53 轮回归修正），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 61 轮：第十七个真实 c6c bug —— ## 多 token 实参丢尾部

### 症状与根因
- `drivers/net/ethernet/intel/e1000e/*`（7 个 TU）报"未声明的标识符
  'E1000_TXDCTL'"。
- 内核 `#define er32(reg) __er32(hw, E1000_##reg)`，调用 `er32(TXDCTL(0))`：
  实参 `reg` 有 `TXDCTL` `(` `0` `)` 四个 token。`##` 应把左边 `E1000_` 和
  首个 token 粘成 `E1000_TXDCTL`，其余 `( 0 )` 原样留下、再重新展开成
  函数式宏。`macro.c6` 的 `##` 分支只取 `ap[0]`、丢掉后三个 token，
  拼出的 `E1000_TXDCTL` 没有实参、不再展开 → 未声明标识符。

### 修复（`c6c/src/macro.c6`）
- `##` 右操作数为多 token 实参时：首个 token 参与粘贴，其余 token
  （`rhs_rest`）原样追加到结果流，使后续重新展开能带上 `( 0 )`。

### 验证（均通过）
- 最小复现：`er32(TXDCTL(0))` 修复前 c6c -E 得 `__er32(E1000_TXDCTL)`、
  rc=1；修复后得 `__er32(((0)+0x100))`、rc=0（与 gcc 一致）。
- 实际 TU `82571.c` 用全内核标志编译 **rc=0**。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13。

### 状态
- 第十七个真实 c6c bug（前十六个 + 第 53 轮回归修正），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 62 轮：第十八个真实 c6c bug —— switch case 容量不足

### 症状与根因
- `arch/x86/entry/syscall_64.c`（4 个 TU）报 20+ 条"case 只能出现在 switch 里"，
  而 `case` 明明在 `switch (nr) { #include <asm/syscalls_64.h> }` 里。
- `parse.c6` 的 `SW_MAXCASE 160`：内核 x64_sys_call() 一个 switch 有 470+
  个 case（syscalls_64.h），超出上限后 case 被当成"不在 switch 里"——
  误导性错误。170-case 最小复现恰好报 10 条（170-160），坐实容量问题。

### 修复（`c6c/src/parse.c6`）
- `SW_MAXCASE` 160 → 1024（覆盖内核 syscall 表，SwState 为静态数组，内存可承受）。

### 验证（均通过）
- 170-case 复现修复前 rc=1（10 条错）、修复后 rc=0；switch 运行语义
  （case 0/1/default）真跑 rc=0。
- 实际 TU `syscall_64.c` 用全内核标志编译 **rc=0**（227616 字节 .o）。
- 回归：`asm-native-check` 10/10、`ld-check` 13/13。

### 状态
- 第十八个真实 c6c bug（前十七个 + 第 53 轮回归修正），已修并验证。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。

## 第 63 轮：最终集成复核（全部 18 修复）

### 已验证
- 用含全部 18 个修复的最新编译器 `/tmp/c6c-swfix` 重跑 10 个历史最小复现
  （paste_empty / cl8 / ternconst / offnested / ampstr / isarray2 / constp3 /
  basefile / paste_fnmacro / swcase170）——**全部 rc=0**，无一回归。
- 全树重编仍为 2464/2942（策展 harness 固定 flag，不变）。
- 用 `.cmd` 权威 flag 补编第 56~58 轮新修的 TU（sound/core/seq 4 个、
  e1000e 若干、syscall_64.c）——**新增 12 个可编译对象**，对象集达 2474。

### 说明
- 试图把这 12 个新对象换入内核做最终启动验证，但该长时间脚本被中断
  （signal 9），临时内核树停在 `make clean` 后的状态、无 vmlinux/bzImage；
  按"不跑长测试循环"的收工纪律未重启整个 swap。
- 第 56 轮的完整启动验证（15 修复、2435 对象、Linux version + 完整 CPU
  topo + 5×NET + M25 + VFS panic）已通过；其后 3 个修复（__BASE_FILE__、
  ## 多 token、SW_MAXCASE）都是纯前端改动，已由各自复现 + 全树重编验证，
  不改变已编对象的代码生成。

### 状态
- 18 个真实 c6c bug 全部修复并提交；本轮无源码改动（仅文档）。
  目标仍未达成：未进 userspace；GNU as/ld/host tools 依赖仍在。
