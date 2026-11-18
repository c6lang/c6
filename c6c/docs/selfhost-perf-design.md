# c6c 自举编译性能优化：详细设计（目标 <1 s）

## 0. 结论先行

自举编译器（host 版 c6c 用 `-O3` 编出来的 `c6c2`）现在编 `c6c/src/c6c.c6`
要 **3.2 s**；目标降到 **1 s 以内**（约 3.2×）。

根因不是前端算法，也不是 ELF 写出，而是 **c6c 自己不内联**：
`C6C_INLINE` 在用 c6c 编译时退化成普通 `static inline`，而 parse.c6 明确写着
“c6c 不做内联”。于是 `pa_cur / pa_peek / tok_eq / pa_is_punct` 这些每 token
调用几十次的小函数，在自举编译器里全是真实函数调用；c6c 的 x64 后端又把所有
虚拟寄存器放在栈槽里、没有寄存器分配，调用固定开销被放大约 20×。

方案：在现有流式 IR 上加一个 **函数内联 pass `ir_inline`**，只挂在 `-O3`，
把 `static inline`/`__always_inline` 的小函数体在调用点展开。`-O0` 完全不动，
保证逐字节基线不变；`-O1/-O2` 保持现状；`-O3` 变强并重新验固定点。

---

## 1. 实测基线（同一台机器）

| 编译器 | 编 c6c.c6 | 编空文件 | 说明 |
|---|---:|---:|---|
| `cc -O2 c6c/src/c6c.c6`（host） | 168 ms | 14 ms | 当前 host 基线 |
| `cc -O0 c6c/src/c6c.c6`（host） | 474 ms | — | 未优化 C 的参照 |
| host c6c `-O3` | 206 ms | 17 ms | 优化 pass 本身的编译开销 |
| 自举 `c6c2`（默认 -O0） | 3179 ms | 323 ms | **本次优化对象** |
| 自举 `c6c2 -O3` | 3916 ms | — | 优化 pass 让编译更慢 |

自举版分阶段（在源码里临时插桩，非最终改动）：

| 阶段 | host | 自举 |
|---|---:|---:|
| `pa_setup` | 0.7 ms | 17.6 ms |
| 预处理 + 解析 + IR + x64 发射 | 156.0 ms | 3197.7 ms |
| └ 其中 `x64_compile` | 14.0 ms | 417.0 ms |
| └ 其中 `ir_optimize`（-O0） | 0.0 ms | 0.0 ms |
| `pa_finish`（回填 + ELF） | 8.2 ms | 13.9 ms |

只跑预处理（`--scan`）：host 24 ms / 自举 289 ms，342075 个 token。
即：**pp 慢 12×，parse 逻辑慢约 20×，x64 发射慢 30×**，三者共同原因是热路径
里的函数调用与栈访问。

---

## 2. 根因细查

### 2.1 `C6C_INLINE` 对 c6c 自己是空操作

`c6c/src/lex.c6:42`：

```c
#if defined(__GNUC__) && !defined(_C6C_)
#define C6C_INLINE static inline __attribute__((always_inline))
#else
#define C6C_INLINE static inline
#endif
```

`parse.c6:7339` 注释确认：“`static inline int f(…)`：c6c 不做内联，`inline` 只是个装饰”。

### 2.2 热函数调用次数巨大

host 侧注释记录：`pa_cur` 在编 `tcc.c` 时调用 648 万次，`pa_is_punct` 319 万次，
`pa_is_ident` 239 万次。编 `c6c.c6` 有 34 万 token，调用次数同数量级放大。
每次真实调用在 c6c 生成的代码里都要建栈帧、写栈槽、跳绝对地址，
而 host gcc -O2 把这些全部内联成几条寄存器指令。

### 2.3 没有寄存器分配

IR vreg 直接映射为栈槽 `X64_SLOT(n) = -16*(n+1)`。函数调用的参数/返回值也走栈。
函数调用越多，栈流量越大，与 2.1 形成放大效应。

---

## 3. 方案设计

### 3.1 总原则

- **不引入 AST 后端**，内联发生在现有 `IrFn` 上。
- **只挂 `-O3`**：`IR_OPT_LEVEL >= 3` 时先跑 `ir_inline`，再跑现有
  `copyprop / fold / jumps / unreachable / dce`。
- `-O0` 输出与基线逐字节相同；`-O1/-O2` 行为不变；每级独立 fixpoint。
- 内联是“编译器优化编译自己”的手段，不是语义要求：被内联函数必须无副作用差异。

### 3.2 前端标记

`Sym` 增加 `u8 is_inline;`。解析函数声明时，既有 `PA_INLINE_SEEN`
（`parse.c6:22133`）或 `inline` 关键字识别处（`parse.c6:7341`）把它写进 `Sym`。
`C6C_INLINE` 展开为 `static inline`，所以这条路径能覆盖全部热函数。

### 3.3 内联候选注册表

在 `ir.c6` 增加：

```c
typedef struct InlineCandidate {
    u32 sym;          /* 函数符号 */
    u16 nops;         /* IR 指令数，超过阈值不收 */
    IrFn body;        /* 深拷贝的 IR（ops/lines/kpool 都复制） */
} InlineCandidate;
```

`pa_emit_fn` 在把 `IrFn` 交给后端前，若函数带 inline 标记且 `n <= INLINE_MAX_OPS`
（初值 64），就复制一份进注册表（分配到 `A_main`，整个编译期有效）。
`x64_compile` 仍照常发射该函数本体——本 pass 不做函数删除，避免打乱符号/地址。

### 3.4 `ir_inline(IrFn *f)` 算法

1. 扫描 `f->ops`，只处理 `IR_CALL` 且目标是注册表中的直接符号调用；
   `IR_CALLI`、递归调用、目标不在表中一律跳过。
2. 对候选调用：
   - 计算展开后 `f->n + callee->n` 是否超过 `INLINE_FN_MAX_OPS`（初值 4096），
     超过则跳过，防止体积爆炸。
   - 为 callee 的每个 vreg 分配 `f` 中的新 vreg（更新 `f->nvreg`），
     建立 `vmap[old] = new`。
   - 为 callee 的每个 label 分配新 label（更新 `f->nlbl`），建立 `lmap[old] = new`。
   - 在调用点前插入 `IR_MOV vmap[param[i]] <- arg[i]`（参数绑定）。
     参数个数取自 `callee->params`。
   - 复制 callee 的每条 IR，按 `vmap/lmap` 重写 `a/b/c/dst/imm`：
     - 普通 vreg 操作数直接换号；
     - `IR_ADDR(flags==0).imm` 与 `IR_ALLOCA.a`、`IR_MEMSET.a` 这类
       **栈槽编号**也要按 `vmap` 换号（它们与 vreg 共用编号空间）；
     - `IR_LABEL` 用新 label；跳转/分支目标用 `lmap`。
   - callee 的 `IR_RET v ...`：
     - 有返回值：插入 `IR_MOV ret_slot <- vmap[v]`，其中 `ret_slot` 是调用点
       原来 `IR_CALL.dst` 的新 vreg（复用它）；
     - 无返回值：不插 MOV；
     - 然后插入 `IR_JMP continuation_label`。
   - 在原 `IR_CALL` 位置放 `IR_LABEL continuation_label`，删掉原 CALL。
   - 同步维护 `f->lines`：复制 callee 行表，插入的伪指令沿用调用点行号。
3.  compaction：原地写出新 `ops/lines`，释放临时数组（用 `A_opt` scratch arena）。

### 3.5 安全约束（明确不做的事）

- **不内联**含 `IR_JMPIND`、`IR_LABELADDR`、变长参数、或 `IR_LEA` 的函数
  （与现有 copyprop 的别名顾虑一致）。
- **不跨递归**：callee 调用自身或调用链成环时跳过（用简单深度栈判定）。
- **不改变符号可见性**：函数体仍发射，调用点只是多了一份拷贝。
- **不内联到 `-O0/-O1/-O2`**。

### 3.6 与现有 pass 的顺序

```
-O3: ir_unreachable → ir_inline → ir_copyprop → ir_fold → ir_jumps
     → ir_unreachable → ir_dce
```

内联放在最前（但在一次 unreachable 之后），让后续 DCE/常量传播吃到内联收益。

### 3.7 若内联不够的二级手段（按优先级）

1. 放宽 `INLINE_MAX_OPS`（64 → 128/256），并加入“调用次数 × 函数大小”启发式。
2. 对非 inline 但足够小、无副作用、无地址暴露的函数也内联。
3. 前端热路径减调用：把 `pa_cur()->kind` 缓存为全局，让 `pa_is_punct/ident`
   直接读缓存（用 `ir_opt_level() >= 3` 守卫，保持 O0 行为）。
4. x64 后端 MOV 合并 / 栈槽复用（peephole）。

---

## 4. 正确性风险与对策

| 风险 | 对策 |
|---|---|
| 栈槽编号冲突 | 同时重映射 vreg 与所有栈槽 imm 操作数 |
| 多返回点 | 每个 `IR_RET` 改成 `MOV + JMP cont`，统一收敛 |
| label 冲突 | 为每个 callee 分配新 label 空间 |
| 调试行号错乱 | `f->lines` 同步扩容/复制 |
| 调试信息/符号重复 | 不删除原函数，只复制调用点 |
| 体积爆炸 | 双层大小阈值 + 递归跳过 |
| fixpoint 变化 | O3 本来就不是基线；重新跑 O3 独立 fixpoint 即可 |
| O0 被污染 | `ir_optimize` 在 `<=0` 时立即返回，注册表也不在 O0 使用 |

---

## 5. 验证计划

1. `cc -x c -O2 -o c6c/build/c6c-opt c6c/src/c6c.c6`
2. `-O0` 与当前基线 `cmp` 逐字节相同。
3. 四级独立 fixpoint：`c6c/tests/selfhost.sh` + `C6C_FLAGS=-O<n>`。
4. `c6c/tests/run.sh c6c/tests/c`：O1/O2/O3 全部 0 fail。
5. CMake `selfhost-opt` / `check-all-c6c` 门禁。
6. 性能：
   - host：`-O0`/`-O3` 编 c6c.c6，≤ 基线 2×；
   - **自举**：host `-O3` 产出 `c6c2`，`c6c2` 编 c6c.c6，目标 <1000 ms；
   - 记录 tiny 文件时间确认不是启动开销。

---

## 6. 实施计划（阶段与门槛）

| 阶段 | 内容 | 门槛 |
|---|---|---|
| P1 | `Sym.is_inline` + 注册表 + `ir_inline` 骨架（只处理无栈槽、单返回的小函数） | 能编译、O0 身份不变 |
| P2 | 完整操作数重映射（栈槽/label/行表）+ 多返回 | O1/O2/O3 fixpoint 与回归全绿 |
| P3 | 接入 `-O3`，测自举时间 | 自举 <1 s；若不够进入 3.7 二级手段 |
| P4 | 文档、CMake 门禁、提交合并 | `worktree finish` |

每阶段结束都要跑该阶段的门槛，不把失败留到下一阶段。

---

## 7. 实验记录：IR 级内联（已否决）

按 §3 实现了完整 `ir_inline`：

- `Sym.is_inline` 标记 + `InlineCandidate` 注册表；
- 调用点展开、参数绑定、多返回改 `MOV + JMP cont`；
- vreg/栈槽/label/行表/常量池完整重映射；
- 修过两个真实 bug：
  1. 已生成的 `IR_ARG` 没删，后面调用的 `narg` 被污染（带 `printf` 的用例段错）；
  2. `IR_ADDR` 的 `a==0` 是“无偏移”哨兵（后端 `if (o->a)`），不能当 vreg 重映射；
     全局地址模式 `flags==2` 也一样，`pa_cur` 因此被错误内联进 while 后自举崩溃。
- 修复后 O0 身份、O1/O2/O3 独立 fixpoint、`tests/run.sh` 全绿。

**但性能结论是否定的：**

| 版本 | 自举编 c6c.c6 | 相对基线 |
|---|---:|---:|
| 无内联（回退后） | 3179 ms | 1.00× |
| `INLINE_MAX_OPS=24` | 3322 ms | 1.05×（更慢） |
| `INLINE_MAX_OPS=128` | 3344 ms | 1.05×（更慢） |

原因：c6c 后端没有寄存器分配，vreg 就是栈槽。内联把 callee 的整套栈槽复制到
caller 帧里，帧变大、栈流量上升；省下的 call 指令远抵不过新增的内存访问。
自举二进制也只增大约 0.5 MB，说明真正热点未必被覆盖到。

**结论**：单纯 IR 内联不是通往 <1 s 的路。已回退这部分代码改动（只保留文档）。

## 8. 修正后的可行路线（待决策）

要在 c6c 现有架构下把 3.2 s 压到 <1 s，需要更底层的改动，按收益/风险排序：

1. **栈槽复用 / 活跃期分析**：让内联或普通调用只占用必要槽位，避免帧膨胀；
   这是内联方案能翻盘的前提。
2. **x64 后端寄存器分配**：把高频 vreg 留在寄存器里，直接削减栈流量；
   预计收益最大，但工作量和回归风险也最大。
3. **热函数专用 calling convention**：小函数用寄存器传参/返回，减少调用开销。
4. **前端减调用**：`pa_cur` 缓存 token kind，让 `pa_is_ch/pa_is_ident_n` 不再每次调
   `pa_cur()`；预计只有 10–20% 量级，不足以单独达标。
5. **预编译自带运行时**：避免每次重解析 `rt.c6`，但会改变 fixpoint 语义，需重新定义
   “自举”边界，不建议。

**诚实判断**：在“不引入 AST 后端、不改 x64 后端栈模型”的约束下，<1 s 目前不可达。
若要继续，建议先做 (1)+(2) 的设计评审，再动代码。

## 9. 宿主 C 编译器对照（2025-11-18 补充）

用三种宿主 C 编译器编出 c6c，再让它们编同一份源码，量化“后端出码质量”的影响：

| 编译 c6c 的宿主 C 编译器 | 被编源码 | 耗时 |
|---|---|---:|
| gcc -O2 | tcc.c | 135 ms |
| tcc -O2 (仓库内 `tcc/c6`) | tcc.c | 719 ms |
| c6c 自举 (`-O3` 产出) | tcc.c | 2381 ms |
| gcc -O2 | c6c.c6 | 201 ms |
| tcc -O2 | c6c.c6 | 890 ms (O0) / 1026 ms (O3) |
| c6c 自举 (`-O3` 产出) | c6c.c6 | 3388 ms |

要点：

- 连 tcc 这种弱优化宿主 C 编译器，产出的 c6c 也比 c6c 自己产出的快 **3.8×**
  （890 ms vs 3388 ms，同一份 c6c.c6）。
- 这说明 3.2 s 的瓶颈在 **x64 后端出码质量**（无寄存器分配、栈槽访问），
  不是前端算法本身；前端在 gcc 宿主版里只要 201 ms。
- 即使完全达到 tcc 的出码水平，c6c.c6 也只到 890 ms，离 1000 ms 目标很近但
  余量极小；要稳定 <1 s 仍需后端改进。

**结论再确认**：<1 s 的可行路径必须改 x64 后端（栈槽复用/寄存器分配），
前端微调（pa_cur 缓存等）只能拿 10–20%，不够。
