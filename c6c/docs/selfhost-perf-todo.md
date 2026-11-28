# 自举编译性能优化 — Todo

目标：自举编译器（host `-O3` 产出）编 `c6c/src/c6c.c6` **< 1 s**。
详细设计见 `c6c/docs/selfhost-perf-design.md`。

## P1 — 基础设施

- [x] 实测基线并定位瓶颈（phase 插桩 + `--scan` + host/self 对比）
- [x] 写详细设计文档
- [x] 写本 todo 文档
- [ ] 开工作树并确认基线源码状态（已开：`dsh/session-...`）
- [ ] `Sym` 增加 `is_inline` 标记，解析 `inline`/`__always_inline` 时写入
- [ ] `ir.c6` 增加 `InlineCandidate` 注册表与 `ir_inline_register()`
- [ ] `pa_emit_fn` 收集小 inline 函数体（深拷贝 IR + lines + kpool）
- [ ] `ir_inline()` 骨架：只处理单返回、无栈槽操作数的直接调用

**门槛 P1**：编译通过；`-O0` 与基线逐字节相同。

## P2 — 完整重映射与正确性

- [ ] vreg 重映射（含 `IR_ADDR` slot imm、`IR_ALLOCA.a`、`IR_MEMSET.a`）
- [ ] label 重映射（`IR_LABEL` / 跳转 / 分支目标）
- [ ] 多返回点改写为 `MOV + JMP continuation`
- [ ] `f->lines` 同步复制/扩容
- [ ] 递归/间接调用/`IR_JMPIND`/`IR_LABELADDR`/`IR_LEA` 跳过逻辑
- [ ] 双层大小阈值与 scratch arena 回收
- [ ] 接入 `ir_optimize()` 的 `-O3` 路径

**门槛 P2**：O0 身份不变；O1/O2/O3 各自逐字节 fixpoint；`tests/run.sh` 全绿。

## P3 — 性能达标

- [ ] 测 host `-O0`/`-O3` 编译时间（≤ 基线 2×）
- [ ] 测自举时间（host `-O3` → `c6c2` → 编 c6c.c6），目标 <1000 ms
- [ ] 若未达标：按设计 §3.7 依次尝试
  - [ ] 放宽 inline 大小阈值 / 加调用次数启发式
  - [ ] 内联非 `inline` 的小纯函数
  - [ ] `pa_cur` token-kind 缓存（用 `ir_opt_level()` 守卫）
  - [ ] x64 MOV 合并 peephole
- [ ] 复测 fixpoint 与回归（每次改动后）

**门槛 P3**：自举 <1 s，且上述门禁仍全绿。

## P4 — 收尾

- [ ] CMake `selfhost-O*`/`check-all-c6c` 门禁复跑
- [ ] 更新 `c6c/docs/design.md` / `c6c/README.md` 中关于“不做优化器/不内联”的旧表述
- [ ] 清理临时插桩代码（`prof_*` 只留在 `/tmp/c6c-prof`，不进仓库）
- [ ] `git status` / diff 复核，提交并 `worktree finish`

## 证据记录

| 项目 | 当前值 | 目标 | 状态 |
|---|---:|---:|---|
| 自举编译 c6c.c6 | 2340 ms | <1000 ms | 进行中（3325→2941→2340） |
| 自举产物磁盘字节 | 3,505,096 | — | 31,088,072→4,005,296→3,505,096 |
| host `-O0` | 168 ms | ≤336 ms | 已满足 |
| host `-O3` | 206 ms | ≤336 ms | 已满足 |
| O0 二进制 vs 基线 | 逐字节相同 | 相同 | 已满足（改动后需复测） |
| O1/O2/O3 fixpoint | 全通过 | 全通过 | 已复测通过 |
| `tests/run.sh` O1/O2/O3 | 0 fail | 0 fail | 已复测通过 |

## 实验结果（2025-11-18）

- [x] 实现完整 IR 内联并修掉两个语义 bug（`IR_ARG` 残留、`IR_ADDR a==0` 哨兵）
- [x] 内联版本通过 O0 身份 + O1/O2/O3 fixpoint + `tests/run.sh`
- [x] 实测：内联使自举时间 3179 ms → 3344 ms（更慢），24-op 阈值 3322 ms（更慢）
- [x] 已回退代码改动，只保留本文档与详细设计

## 修正后的计划

- [ ] 方案 (1)：栈槽活跃期分析 / 复用，先出设计再实现
- [ ] 方案 (2)：x64 后端寄存器分配（收益最大，风险最大）
- [ ] 方案 (3)：小函数寄存器调用约定
- [ ] 方案 (4)：`pa_cur` token-kind 缓存（量级小，可与其他方案叠加）
- [ ] 每步都复测自举时间与四级 fixpoint

**当前结论**：<1 s 尚未达成；单纯内联被实测否决。

## 补充实验（2025-11-18）

- [x] 宿主编译器对照：tcc 编出的 c6c 编 c6c.c6 890 ms（O0）/ 1026 ms（O3）；
      self 编出的 c6c 3388 ms。差距 3.8× 来自后端出码质量。
- [x] `pa_cur()` 宏快速路径（用已有 PA_CURP 缓存）实测 3323 ms（噪声内，无有效收益），已回退。
- [x] 当前代码已回到干净基线：O0 身份、O1/O2/O3 回归与 O3 fixpoint 全绿。

**结论**：<1 s 仍未达成；前端级手段（内联、pa_cur 宏）均无效，
必须改 x64 后端出码模型（栈槽复用 / 寄存器分配）。

## 暂停决定（用户选择）

- [x] 用户选择“暂停并保留现状”：不再做 x64 后端寄存器分配等大改。
- [x] 代码改动已全部回退，worktree 只保留设计与实验记录文档。
- [x] 当前基线门禁全绿：O0 身份、O1/O2/O3 fixpoint、tests/run.sh 全通过。
## 后续（2026-11-28）：先做不改 IR/ABI 的后端窥孔

- [x] 纯帧槽地址不物化：`ADDR` 只喂 `LOAD`/`STORE` 时直接 `[rbp+disp]`
- [x] `CMP`+`BR` 融合、`BR` 直接 `cmp qword [slot],0`
- [x] ALU/MUL/DIV/CMP 内存操作数、小常量、单指令 `movsx`/`movzx`/`movsxd`
- [x] 无内联汇编的函数按需保存 callee-saved（省 5 push/pop + dummy）
- [x] 微基准：`main` 指令数 loop 123→67 / fib 47→26 / call 123→66 /
      branch 174→97 / mem 203→122；耗时 1.17–1.56×；`-O0..-O3` fixpoint 全绿
- [ ] 仍未做：线性扫描寄存器分配 / 栈槽复用（收益最大的部分，P2-2b 主体）

## 第二轮（2026-11-28）：合并分析 + disp8 + 常量/跳转消除

- [x] 三趟分析合并成一趟，缓冲跨函数复用；`an_slot` 压成 u32（bit0=1 表示纯槽），
      读次数压成 u8（饱和到 2），不再每函数 malloc/free
- [x] 帧槽偏移 -128..127 用 mod=1 + disp8（`x_mem_modrm`），每条访存再短 3 字节
- [x] 常量只喂一条 `STORE` 时直接 `mov [rbp+disp], imm32`
- [x] `JMP` 目标为下一条标签时整条省掉；`CMP+BR` 的真/假块紧邻时反转条件码/省 jmp
- [x] 实测：`main` 指令数 loop 123→64 / call 123→62 / branch 174→93 / mem 203→112；
      c6c `.text` 4,732,306→2,990,508（-36.8%），tcc.c ELF 3,221,472→2,075,856（-35.6%）
- [x] 编 tcc.c 的 wall time：min 138.3→139.4 ms（+0.8%）、p25 139.7→141.1 ms（+1.0%）；
      分析约 2 ms。135 ms 门槛原本就在基线噪声边缘（基线自身 138.3）
- [ ] P2-2b 主体（线性扫描寄存器分配 / 栈槽复用）仍未做

## 第三轮（2026-11-28）：零静态进 BSS + 块内待定值

- [x] **BSS**：`elf_hdr_fill()` 加 `bss` 参数，`p_memsz = p_filesz + bss`；
      `x64_bss_sym()` 登记候选、`x64_bss_layout()` 收尾统一分配；`pa_bss_ok()`
      把 `-c`/`-S`/`-c99`/LLVM 四条要读回零字节的路挡在外面
- [x] 顺序约束：`x64_resolve_blobs()`（会往代码段追加跳板）必须在
      `x64_bss_layout()` 前，`data_fixups_apply()`（数据里可能存 BSS 绝对地址）
      必须在后；两条错了产物就不确定
- [x] **块内待定值**：`pv`（rax 里留着哪个 vreg）+ `pd`（槽里副本过期）；
      读命中省一条 load，延迟写槽；只定义一次且只读一次的值连写回也省
      （`x64_pend_dead()`，靠一次性分析的 `an_one`/`an_use`）
- [x] 块边界一律冲掉：`LABEL`/`JMP`/`JCC`/`RET`/`JMPIND`/`CALL`/`CALLI`/内联汇编/
      `SETRSP`/`VAARG`/`ALLOCA`/`DIV·MOD` 之前 `x64_pend_flush()`，不做跨块活性推理
- [x] 踩坑：调试期在 `x64_pend_dead()` 里留了个 `getenv()`，每个 IR op 扫一遍
      environ，自举直接从 2.4 s 涨到 4.1 s（perf 里 44% 在 getenv 的 strcmp 循环）。
      删掉后回到 2.34 s —— 出码逻辑本身的开销只有几十毫秒
- [x] 三向实测（同一份源码、同一命令、3 次中位）：自举 **3325→2941→2340 ms**（-29.6%）；
      产物 **31,088,072→4,005,296→3,505,096** 字节（-88.7%，其中待定值这一层
      -12.5% 是真实代码/数据收缩，BSS 那 -87.1% 是零字节不再落盘）
- [x] 同一份源码的老/新出码对照：2963–2986 ms→2379–2406 ms（-19.6%）；
      fib(27) 5.96→4.65 ms（-22%）；微基准产物 -12.4%
- [x] `-O0..-O3` fixpoint、`check-all-c6c`（std/test/diag/c99suite/tests1·2/
      build-tcc 三代）全绿
- [ ] 待定值只有 1 个槽位；扩到 rax+rdx 两个能覆盖 `a op b` 的双操作数
- [ ] P2-2b 主体（线性扫描寄存器分配）仍未做：局部量的 load/store 一条没少
