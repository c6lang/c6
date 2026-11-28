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
| 自举编译 c6c.c6 | 1369 ms | <1000 ms | 进行中（3325→2941→2340→1617→1369） |
| 自举产物磁盘字节 | 3,235,224 | — | 31,088,072→4,005,296→3,505,096→3,234,336→3,235,224 |
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

## 第四轮（2026-11-28）：运行期原语走 rep（自举 2340→1686 ms）

先量化再动手：`perf stat` 显示自举时 gcc -O0 编的 c6c 是 2.10 G 条指令、
c6c 自举编出的 c6c 是 9.91 G 条（**4.71×**），IPC 都是 1.6 左右——差的全是
指令条数，不是缓存/分支。再按地址看热点：`c6c_strlen`（逐字节
`while (s[n]) n++`）一个函数就占 **18%**，`memset`（逐字节 `d[i]=c`）占 5%。
两条循环在 c6c 自己的出码下分别是 14 / 18 条指令**每个字节**。

- [x] 新增两块汇编跳板（三个后端各一份，`src/asmblob.c6`）：
      `__c6_strlen_rep`（`repne scasb`，不越过 NUL，长度由 rcx 反推）、
      `__c6_memset_rep`（`rep stosb`）；GAS 文本同步，c99suite / LLVM /
      `-S` 三条路一起验
- [x] `lib/rt.c6`：`c6c_strlen` 直接走跳板；`memset` 在 n≥8 时走 `rep stosb`；
      `memmove` 的不重叠分支（目的在前）改走 `c6c_mem_copy`（8 字节块 / rep）
- [x] 顺手补两个待定值漏点：`IR_LOAD` 的地址操作数用 `x64_pend_take()`
      （`&a[i]` 的 LEA 后紧跟读不再从槽里重读），`IR_STORE` 的地址正好待定时
      只读值不读地址
- [x] 实测（同命令、3–5 次中位）：自举 **2340 → 1686 ms（-28%）**；产物
      3,505,096 → **3,348,288** 字节；`fast` 门禁全绿（std/tests1·2/
      c99suite/LLVM/asm/obj 都过）
- [ ] 再剖析已经很平（最高一项 2.3%）：剩下的只能靠"每条 IR 少发指令"，
      即 P2-2b 主体的线性扫描寄存器分配（预估 1.5–2×，落到 0.9–1.1 s）

## 第五轮（2026-11-28）：无操作拷贝/截断合并（体积 -3.4%，时间持平）

- [x] `x64_alias_pass()`：`MOV`（同类型）/`BITCAST`/`TRUNC`·`SEXT`·`ZEXT`
      （目标类型等于源的定义类型且源头已规范位宽）的 dst 合并到源头，读点全部
      改读源头，整条指令和它的写槽一起消失；变成自拷贝的整条 NOP 掉，
      读次数重算（CMP+BR 融合与"只读一次不写回"依赖它）
- [x] 踩坑一：`vreg 0` 是"没有这个操作数"的哨兵（`IR_ADDR.a`/`IR_LEA.b`），
      也是局部量的合法槽号。把 0 合并成别的 vreg 会让所有哨兵变真操作数
      （54_goto 的字符串地址被加了局部变量 5678）。现在 0 既不合并也不被合并
- [x] 踩坑二：`TRUNC I32 ← CONST I32` 不是空操作——`const` 只发
      `mov r32,imm32`（零扩展），符号位要靠那条 trunc 补。只有 LOAD /
      CALL·CALLI / SEXT·ZEXT·TRUNC 这些真发 `movsx/movzx/movsxd` 的定义才算
      "源头已规范位宽"（`x64_canon_def()`）
- [x] 实测：自举 1620 → 1640 ms（噪声内，**没有速度收益**），产物
      3,348,480 → **3,234,336** 字节（-3.4%）；`fast` / selfhost O0–O3 fixpoint
      全绿
- [ ] 结论：剩下的 1.6× 只能靠块内寄存器分配（每条 IR 少发 load/store），
      继续做这种"少一两条指令"的窥孔已经到顶

## 第六轮（2026-11-28）：标量数组初始化叶子快路（自举 1617→1369 ms）

指令数是硬指标（`perf stat`：gcc -O0 编出的 c6c 2.19 G 条，自举编出的 6.28 G
条，IPC 都是 1.4–1.6，差的全在条数）。按函数看 -O0 剖析，`init_leaf_walk`
一个函数占 **10.5%**——它是初始化器的"类型叶子展开"，c6c 自己的大静态表
（关键字表/类型表/符号初始值）全是标量数组，原来每个元素都要递归一次真调用。

- [x] 标量元素数组（`!ty_is_agg && !ty_is_array`）不递归，直接按步长把
      LF_OFF/LF_TY/LF_BITS/LF_BOFF 批量填进去；LF_TRUNC 的边界检查照样做
- [x] 实测：自举 1617 → **1369 ms（-15.3%）**；产物 3,235,224 字节；
      selfhost 逐字节 fixpoint 通过，fast 门禁全绿（std/tests1·2/c99suite/
      LLVM/asm/单元测试）
- [ ] 下一批目标（-O0 剖析）：`gen_postfix` 7.3% / `gen_unary` 4.6% /
      `parse_typespec` 4.1% / `lex_next` 2.6%；`x64_alias_pass`+`x64_count_use`
      自己要吃掉 3.3%，可以合并成一趟
- [ ] 块内寄存器池试过一版（4 个 caller-saved），实测 **变慢**
      （1617→1942 ms、产物 +13%）：出码器每条 IR 的簿记在 -O0 自举价位上太贵，
      而且遇到 call/label/未改造 op 全冲池。要做就得把分配放到剖析阶段离线
      算好、出码只查表（O(1)），否则得不偿失

## 第七轮（2026-12-02）：常量走立即数 + 热谓词去嵌套（自举 1357→1077 ms）

本轮换了个口径：先确认"自举"量的就是 `c6c2`（自举出来的 c6c）再编一遍
`src/c6c.c6`，把 `perf stat -e instructions:u` 当主指标（墙钟受机器负载影响
±30 ms，指令数稳定）。起点 **1357 ms / 3.9 G 条**，终点 **1077 ms / 3.43 G 条**
（-6.2%），产物 3,235,344 → 2,224,835 字节。

**A. 常量不再过帧槽（-14%）** —— 这是本轮唯一的大头。自举时热点全是
`t->kind != TK_PUNCT`、`n == 1` 这种标量常量比较，原来每条 `const` 都要
`mov r32,imm32` + 写槽，比较再从槽里读回来：

- [x] `x64_analyze()` 登记"只定义一次、值能当 imm32 用"的常量（`an_imm`/
      `an_immok`），新增 `x64_imm_of()`；
- [x] 比较的第二操作数（`x64_cmp_operand`）和加/减/与/或/异或的第二操作数
      直接用 `81 /7 id`、`83 /7 ib` 立即数形式；
- [x] 别名合并新增"常量 → 截断/扩展"也是空操作（`x64_const_fits`）：`const
      i32 5` 转 i64 之后就并到常量上，比较随即吃上立即数；
- [x] 能当 imm32 用的常量直接 `mov qword [rbp+disp], imm32` 进帧槽，不碰
      rax（省一条 mov，也免得把 rax 里的待定值冲回槽再读一次）；
- [x] `IR_MOV` 的源是这类常量时直接写目标槽；常量只喂一条比较/算术/MOV 时
      这条 const 干脆不发码；
- [x] 踩坑：跳过 const 时**不能**跟着 `i++` —— 那条写的是"和下一條融合"，
      而这里是把活留给下一條自己干，多走一步就把消费者整條丢了（症状：
      自举产物缩小 27%、段错误）
- [x] 踩坑：`x64_const_fits` 里"更窄的目标"不能当空操作，`u32` 负常量也
      不行（槽里存的是符号扩展值，u32 的规范形式是零扩展）

**B. 前端热路径（-3.5%）**：

- [x] `tok_eq_len` 按长度分支成直落比较（n≤4 无循环）
- [x] `pa_is_punct`/`pa_is_ident_n` 的逐字节比较就地展开，不再套 `tok_eq`/
      `tok_eq_len` 两层；`pa_accept_ch` 的判断摊进函数体
- [x] `tok_data`/`pa_cur` 从 `static inline` 改成宏（266 / 338 个调用点）
- [x] 6 个 `C6C_DBG_*` 开关的 `getenv` 结果缓存进静态量（getenv 光自己就占 3%）
- [x] `memchr` 改 8 字节一混（按行扫源码那一趟）；`img_u8/u16/u32` 把
      `img_reserve` 的判断就地摊开（产物三兆字节 = 三百万次调用）；
      `sym_hash_str` 改 8 字节一混的 FNV-1a
- [x] 踩坑：把 `pa_is_ch`/`pa_is_punct`/`pa_accept_*` 全改成宏（700 多个
      调用点就地展开）指令数只少 0.5%，但代码涨 180 KB，**墙钟反而慢 4%**
      —— 热代码被摊开，I-cache 兜不住。这条路退回来了，只在注释里留结论。
      同理 `TOK_EQ` 加"先比长度+首字节"的短路保护也是**变慢**（+46 M 条），
      因为命中的调用点付两份钱

**下一步（-7% 才到 1 s）**：剖析已经摊平，没有单点大肉。按条数看：token
谓词族 19%（`pa_is_*` 13.6% + `tok_eq_len` 5.4%，动态调用约两千万次，每次
只剩"调用开销 9 条 + 函数体 13 条"）、词法 7%、`gen_postfix`+`gen_unary`
7.7%、符号表 4%、x64 后端 8%。要么在**最热的几个调用点**定点内联（不能全
展开），要么把 token 的关键字/punct 分类在词法阶段算好、谓词退化成一次整型
比较 —— 后者要动 Tok 结构和 340 个 `TOK_EQ` 点，得单独一轮做。

## 第八轮（2026-12-03）：剖析换口径 + 两处"少一条指令"的死路

**A. 拿到可信的剖析数据（方法本身是本轮最大的产出）**

自举产物是 section-less ELF，perf 按符号归属的百分比**不可信**：`pa_cur_slow`
被算成 0.91%，而 callgrind 数出来的真实调用次数是 **13 次**。可用的口径是：

    valgrind --tool=callgrind --callgrind-out-file=/tmp/cg.out \
        /tmp/c6c_ld -fsyntax-only lib/rt.c6        # 小输入，避免 brk 溢出
    nm -n --defined-only /tmp/c6c_ld                # callgrind 只给地址，自己反查

跑 `src/c6c.c6` 会让 valgrind 撞 brk 上限（"brk segment overflow"），
`-o`/`-c` 也跑不动；`-fsyntax-only` 一个中等文件（rt.c6，3.35 亿条）能跑完，
比例结构与自举那一趟一致。得到的**精确**数字（rt.c6，自举编译器）：

| 项目 | 每函数指令 | 调用次数 | 占比 |
|---|---:|---:|---:|
| pa_is_ident_n | 44 | 471,973 | 6.28% |
| tok_eq_len | 36 | 428,480 | 4.57% |
| x64_analyze | — | 430 | 4.17% |
| init_leaf_walk | — | 334 | 3.72% |
| pa_is_punct | 67 | 170,540 | 3.42% |
| lex_next | 250 | 44,058 | 3.27% |
| c6c_mem_copy | 57 | 179,048 | 3.04% |
| memchr | 540 | 18,837 | 3.03% |
| pa_accept_ch | 54 | 171,846 | 2.78% |

一个 token 平均要过 **36 次 token 谓词调用**（472+428+231+172+171+114 千次
/ 44 千 token）。表达式优先级链（gen_assign→…→gen_postfix 共 14 层）
占 gen_assign 的 **15%**（132M 里 50M 是每层的穿透开销）。

**B. 两处死路（都没有留下代码）**

- [x] 延迟 lea：`lea dst,base,disp` + 紧接着的 `load dst` 折成一条
      `mov rax,[rbp+槽(base)+disp]`（理论上省 2 条）。压栈试了：
      自举产物**编不出任何东西**（"没有 main"）。逐点打印折叠处出的机器码
      （`movzx eax,word [rbp-0xf54]`）**完全正确**，把快路关掉只留状态机仍然坏。
- [x] `lea rax,[rbp+槽(base)+disp]` 直接代替「load 基址 + `lea rax,[rax+disp]`」：
      数学上完全等价，**也坏**。
- [x] 对照：把 `x64_pend_take` 换成 `flush + 显式 load + lea`（语义一模一样）
      则是好的 —— 说明问题不在 pend 状态机，而在**代码长度/偏移一变**就踩到
      的某个东西（字节级 fixpoint 不报，但产物编不动）。下一轮要查这个的话，
      先看 rel32/label 回填和 `x64_label_next` 的跳转省略是不是对长度敏感。
- [x] 顺手把 `x64_compile` 里每函数一次的 `getenv("C6C_DUMP")` 缓存掉
      （自举 3.428 → 3.413 G 指令，-14.7 M，约 -0.4%）；**墙钟 1077 → 1056 ms**

**C. 下一步的着力点（按本轮实测）**

- 谓词族仍是 19%：36 次/ token × 40~60 条。要动就得让「token 分类」变成
  O(1) —— 在词法阶段给每个 token 算一个关键字/标点 id，`TOK_EQ` 与
  `pa_is_*` 退化成一次整型比较。这要动 Tok 结构和 340 个 TOK_EQ 点。
- 优先级链 15%：14 层函数穿透可以改成一张优先级表 + 一个 climb 循环
  （每个操作数省掉 10 层左右的 pa_cur/谓词）。**注意**：任何改代码长度的
  改动都要按本轮 §B 的经验做完整的自举验证，不能只看 fixpoint。
- 后端 `x64_analyze` 4.2% + `x64_alias_pass` 2% + `init_leaf_walk` 3.7%：
  都是整趟扫描，可以合并成一趟（todo 里挂了很久）。
