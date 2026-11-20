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
