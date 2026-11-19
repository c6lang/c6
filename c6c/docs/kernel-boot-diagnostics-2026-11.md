# c6c 内核启动诊断记录（2026-11）

## 已修复并合入 main 的 c6c 编译缺陷
- bsr/bsrq、tzcnt/bsf、bts/popcnt 指令编码与 ALTERNATIVE popcnt 提取
- static UND 符号不再发成 STB_LOCAL UND（曾导致 `call 0`）
- 空声明布局、内联 asm `+r` 输出写回变量真实帧槽、CR/端口 IO 识别
- 跨 TU 别名 / static inline / 8 字节 struct 返回 ABI
- PIE / 栈对齐 / 重定位 / 段对齐启动修复
- 保留 WASM 后端与 C99_WASM_NARGS
- **复合字面量零初始化**：表达式位置 `(T){...}` 先 `gen_zero_obj(addr, ty_size(ty))`
  再铺初始化器（commit ee62fba）。未提及成员不再残留栈垃圾。

验收：`cmake --build out --target c6c -j8` 通过；`ctest -R c6c-tests2`
失败项与基线一致（119_random_stuff / 138_arm64_encoding / 61_integers /
95_bitfields / 98_al_ax_extend / 99_fastcall），无新增回归。

## 已达成
- 全内核 C 源码可由 c6c 编译、链接出 vmlinux/bzImage（链接仍用 GNU ld）。
- 最小全 c6c 内核在 QEMU（`-cpu Nehalem`）串口输出含 `Linux version`。
- 修复复合字面量后，SLUB `pool_workqueue` 的 `ctor=0x8` 早期 #PF 消失
  （无需任何内核源码 workaround，纯编译器修复）。

## 未达成 / 根因已定位
1. **内核未真正可启动**（串口只有手工早期 COM1 直写的一行 banner）：

   - `__attribute__((section("...")))` 被 c6c 的 `skip_attributes()` 静默丢弃
     （parse.c6 的 `sa_is_kw` 表无 `section`，属性体扫描无对应分支，落到通用
     `pa_advance()`）。`.init.setup` / `.earlycon_table` / `__X_sched_class` /
     `.initcall*` 全部落进 `.data`，earlycon/console 启动参数注册失效。
     修复需给 obj.c6 增加可变命名段（见下）。

   - UND weak 函数引用被内核模式 DCE 折叠成一个 `0xC3` ret-stub 并以 weak
     定义发出：`timekeeping.o` 里 `clocksource_default_clock` 与
     `vdso_time_update_aux` 同址（`0x4dc3`，都 `W`）。GNU ld 在两个 weak 定义里
     挑了 ret-stub（而非 jiffies.o 的真实实现），`clocksource_default_clock()`
     返回栈垃圾 `0xffffffff84800ba8`，`tk_clock_read` 的 `call *%r11` 跳进去 #PF。
     在 /tmp/c6c-min 把 jiffies.c 的 `__weak` 去掉（变强符号 `T`）后该 fault 消失，
     内核连续运行 60 秒不再 panic。**这是 c6c 的弱符号/ret-stub 设计缺陷**：
     对「有真实定义的外部 weak 函数」不能折叠成 ret-stub。

   - 当前卡点：`calibrate_delay_converge()` 的 `while (ticks == jiffies);` 不前进，
     `EFLAGS=0x2`（IF=0）。`start_kernel` 的 `local_irq_enable()` 展开的
     `arch_local_irq_enable` static inline 没有落 `sti`。手工在 1205 行与
     `calibrate_delay()` 前各补一条 `asm volatile("sti")`，vmlinux 里能看到两条
     `sti`（0x81012b6f / 0x8101318d），但停在循环时 IF 仍为 0——待确认是 GDB
     停止时清了 IF，还是中途又被 `cli`。`arch_local_irq_enable` 有多个本地副本
     （每个 TU 一份），怀疑跨 TU 符号解析选错了副本。

2. **工具链依赖未消除**：
   - c6c 没有独立汇编器；`.S`（含 ELF32/16 位实模式）仍用 GNU as。
   - c6c 的 ld.c6 仅 ELF64 静态链接、最多 64 对象，不支持链接脚本、`.a`、
     `--gc-sections`、kallsyms 多遍、ELF32；内核链接仍用 GNU ld。
   - host tools（modpost/relocs/objtool）仍由 GCC 构建。
   这些是全新实现，单次会话剩余轮次无法完成。

## 建议后续顺序
1. 修复 UND weak 函数引用：有真实外部定义的 weak 函数不能折叠成 ret-stub，
   必须保留真实 call 交给链接器（类似全局 UND 的处理）。
2. 修复 `arch_local_irq_enable` 跨 TU 副本选择 / `sti` 丢失。
3. 给 obj.c6 增加命名段（变量优先，约 150-220 行，追加在 19 之后避免重编号）：
   - `SAW_SECTION` 捕获 + `X64Sym.sect_idx/size`；
   - `ObjSec` 描述符数组替代固定 19 项表；
   - 从 DATA_IMG 挖出具名字节并重写 `st_value` / `r_offset`。
4. 新建汇编器与内核级链接器（最大工作量）。
