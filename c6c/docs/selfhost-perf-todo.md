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
| 自举编译 c6c.c6 | 3179 ms | <1000 ms | 未开始 |
| host `-O0` | 168 ms | ≤336 ms | 已满足 |
| host `-O3` | 206 ms | ≤336 ms | 已满足 |
| O0 二进制 vs 基线 | 逐字节相同 | 相同 | 已满足（改动后需复测） |
| O1/O2/O3 fixpoint | 全通过 | 全通过 | 改动后需复测 |
| `tests/run.sh` O1/O2/O3 | 0 fail | 0 fail | 改动后需复测 |

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
