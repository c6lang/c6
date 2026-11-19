# c6c 内核启动诊断记录（2026-11）

## 已修复并合入 main 的 c6c 编译缺陷（commit 2d32e95 及祖先）
- bsr/bsrq、tzcnt/bsf、bts/popcnt 指令编码与 ALTERNATIVE popcnt 提取
- static UND 符号不再发成 STB_LOCAL UND（曾导致 `call 0`）
- 空声明布局、内联 asm `+r` 输出写回变量真实帧槽、CR/端口 IO 识别
- 跨 TU 别名 / static inline / 8 字节 struct 返回 ABI
- PIE / 栈对齐 / 重定位 / 段对齐启动修复
- 保留 WASM 后端与 C99_WASM_NARGS

验收：`cmake --build out --target c6c -j8` 通过；`ctest -R c6c-tests2`
失败项与基线一致（119_random_stuff / 138_arm64_encoding / 61_integers /
95_bitfields / 98_al_ax_extend / 99_fastcall），无新增回归。

## 已达成
- 全内核 C 源码可由 c6c 编译、链接出 vmlinux/bzImage（链接仍用 GNU ld）。
- 最小全 c6c 内核在 QEMU（`-cpu Nehalem`）串口输出含 `Linux version`。

## 未达成 / 根因已定位
1. **内核未真正可启动**：在 `console_init` 前 page fault，串口只有手工早期
   COM1 直写的一行 banner。剩余链路：
   - `__attribute__((section("...")))` 被 c6c 的 `skip_attributes()` 静默丢弃
     （parse.c6 的 `sa_is_kw` 表无 `section`，属性体扫描里无对应分支，落到
     通用 `pa_advance()`）。结果：`.init.setup` / `.earlycon_table` /
     `__X_sched_class` / `.initcall*` 全部落进 `.data`，earlycon/console 的
     启动参数注册失效。修复需给 obj.c6 增加可变命名段（详见子代理研究）。
   - SLUB 早期 `setup_object()` 间接调用 `s->ctor == 0x8` 的故障：根因是
     `KMEM_CACHE(pool_workqueue, SLAB_PANIC)` 的
     `&(struct kmem_cache_args){ .align = __alignof__(...) }` 复合字面量在
     内核/DCE 上下文中未完整清零，`.ctor`（偏移 24）残留栈垃圾 8。
     已在 /tmp/c6c-min 用命名局部（c6c 对 `&local` 会 `rep stos` 清零）绕过，
     故障消失，启动推进到时钟源函数指针 fault（下一处待查）。
   - 复合字面量/指定初始化器的局部聚合对象未零初始化未提及字段：
     独立复现 `&(struct A){.align=256}` 只写偏移 0，余下字节保留栈垃圾；
     而 `struct A a = {}` 会发出 `rep stos` 清零。这是一般性 c6c 正确性缺陷，
     推荐修复点：`gen_init_obj` / `gen_local_braced_new`（parse.c6 ~18770）
     在聚合初始化前先按 `ty_size` 清零目标，或补齐 `init_leaf_walk` 的零填充。

2. **工具链依赖未消除**：
   - c6c 没有独立汇编器；`.S`（含 ELF32/16 位实模式）仍用 GNU as。
   - c6c 的 ld.c6 仅 ELF64 静态链接、最多 64 对象，不支持链接脚本、
     `.a`、`--gc-sections`、kallsyms 多遍、ELF32；内核链接仍用 GNU ld。
   - host tools（modpost/relocs/objtool）仍由 GCC 构建。
   这些是全新实现，单次会话剩余轮次无法完成。

## 建议后续顺序
1. 修复复合字面量局部聚合零初始化（小而通用，直接消除 `ctor=0x8` 类故障）。
2. 给 obj.c6 增加命名段（变量优先，约 150-220 行，追加在 19 之后避免重编号）：
   - `SAW_SECTION` 捕获 + `X64Sym.sect_idx/size`；
   - `ObjSec` 描述符数组替代固定 19 项表；
   - 从 DATA_IMG 挖出具名字节并重写 `st_value` / `r_offset`。
3. 逐段排查启动早期 fault（时钟源、workqueue、页表）。
4. 新建汇编器与内核级链接器（最大工作量）。
