# c6 引用计数 GC 实施 todo

设计见 [gc-design.md](gc-design.md)。本文是执行清单：阶段、改动点、测试、验收命令。
每个阶段独立可编译、可回归、可提交。

约定：

- 构建（c6 模式）：`cd tcc && make clean && make -j$(nproc) EXTRA-DEFS=-DC6_EXT CFLAGS="-g -O0"`
  或在仓库根 `make build`（`MODE := tcc/.c6-mode` 会在模式切换时自动 clean）。
- 验收（每阶段都必须绿）：

  ```sh
  make selftest                      # tests/run-tests.sh，原生后端
  BACKEND=c99 tests/run-tests.sh     # C99 后端
  make test-plain                    # 关掉 C6_EXT 的零影响回归
  ```
- 新测试的位置与写法按现有约定：
  `tests/*.c`（`test "desc" {}` 块）、`tests/errors/*.c`（`// EXPECT <frag>: error: <text>`）、
  `tests/c99/*.c`（`// EXIT` / `// OUTPUT`）。
- 提交：一个阶段一个提交，标题 `gc: Pn <一句话>`，正文写验收命令与结果。

---

## 阶段总览

| 阶段 | 内容 | 依赖 | 主要风险 |
|---|---|---|---|
| P0 ✅ | 类型/限定符/描述符脚手架 | — | `parse_btype` 与 `@` 的冲突 |
| P1 ✅ | `new` + 存储 + 内联释放 + `p = NULL` | P0 | 作用域退出点的完整性（goto/err） |
| P2 ✅ | 描述符、字段释放、数组 `new T[n]` | P1 | 拓扑排序其实不需要 |
| P3 ✅ | `weak`（含禁 async） | P2 | 帧搬动只能探测，不能 trap |
| P4 | 析构 `drop` 方法 | P3 | 重入、跨线程调用 |
| P5 | `shared` + 跨帧/跨线程转移 | P2 | `go` 实参块与任务 slot 的 +1/−1 |
| P6 | 静态环检测（硬错误）+ 链接期 | P2/P3 | `own` 语义、跨 TU 段 |
| P7 | async/go 集成与预算化 drain | P1/P5 | `__c6_co_abort` 回卷 |
| P8 | 文档与全量回归 | 全部 | — |

---

## P0 类型与限定符脚手架

改动点：

- `tcc/tcc.h`
  - 复用空闲类型位：`VT_GC 0x00040000`、`VT_GCWEAK 0x00080000`、
    `VT_GCOWN 0x00100000`（先确认 `Sym.r`/`type.t` 的位没有别的用途；
    不要用 `mk_pointer()` 构造，照 `c6_global_pointer` 的做法）；
  - `SValue` 增 `unsigned char gc_state;`（BORROW/FRESH/OWNED）与 `gc_src` 溯源位；
  - `TCCState` 增 `C6GcType` 动态数组（tag、tuple 化的字段列表、file:line、flags）；
- `tcc/tccgen.c`
  - `parse_btype()`（:5391）识别 `@` 前缀与 `weak`/`own`/`shared` 软关键字；
  - `type_decl()`（:6037）允许 `@T *`/`@T` 的声明写法；
  - `type_to_str()`（:2998）打印 `@T`、`shared @T`、`weak @T`、`own @T`；
- `tcc/tccpp.c` 预定义：`struct __c6_gc_desc`、`struct __c6_gc_field`、
  `__c6_gc_*` 原型（照 `__c6_co_*` 的 `#ifndef __TCC_ASYNC_DEFINED` 手法加
  `__TCC_GC_DEFINED`）；
- 非 c6 模式（无 `C6_EXT`）零改动。

测试：`tests/typename.c` 增两条（`@int` / `weak @Node` 的类型字符串打印）。

验收：`make build && make selftest && make test-plain`。

---

## P1 分配、存储、释放 ✅

**落地状态（与计划的差异）**

- `CType.gq` 只是缓存，托管性质的可信副本放在**被指符号**上，判定走
  `c6_type_gq()`；编译器内部要把托管类型当裸指针用时用 `c6_gc_plain()`。
  理由与踩坑见设计 §2.0。
- 增加运行时 `mode` 位 `C6_GC_OWN=8`、`C6_GC_INIT=16`（见设计 §4.1/§4.2.8）。
- 作用域释放**复用了已有的 defer 记录机**（`__tcc_defer_push` + 记录体里
  调 `__c6_gc_release`），因此 `return`/`break`/`continue`/`goto`/`err`
  和块退出用的是同一套调度，一处都不用改；代价是托管局部量会占一条记录
  （每条 16 字节，块退出即回收）。
- 实参里的 FRESH 值先搬进独立帧槽再传值，调用返回后统一释放；这次释放是
  一次调用，所以返回值先存槽、释放完再取回（`c6_gc_arg_release(has_value)`）。
- `c6_gc_return_hook()` 只负责 incref（把一份引用交给调用者）；返回槽的
  存储必须走 `c6_gc_plain()`，否则会拿未初始化的旧值去 decref。
- `__c6_gc_live` / `__c6_gc_refcount` 返回 `unsigned long long`（tcc 把
  `unsigned long` 归一成 `VT_LLONG`，C99 后端打印原型时必须一致）。
- 已知推迟项：`async` 体里的托管局部量在帧搬动/回卷时的转移（P7），
  `defer` 记录 8192 条静默丢弃（H3，P7 一起修）。

改动点：

- `tcc/tccgen.c`
  - `unary()` 的 `new` 分支：`new T`、`new T[n]`、`new shared T`，
    结果 `gc_state = FRESH`；
  - `vstore()`（:4096）托管分派：强存储/弱存储/原子存储/own 存储，
    生成 `__c6_gc_store(&lvalue, value, mode)`；
  - `expr_eq()`：托管左值的赋值走同一路径；
  - `init_putv()`（:12107，动态分支 :12244）：局部初始化与
    `@T p = NULL;`（强制初始化的零值形态）；
  - **内联释放**：`block()`/`decl()` 的作用域退出（`prev_scope_s`）、
    `TOK_RETURN`（:11367）、`break`/`continue`、`TOK_GOTO`（沿作用域栈逐层）、
    `err` 传播路径 —— 逆声明序发射 `__c6_gc_release(&local, mode)`；
  - FRESH/借用检查：`gfunc_param_typed()`（:6151）、`?:`、条件上下文、`sizeof`；
- `lib/libtcc1.c` + `tcc/c99-rt.inc`：`__c6_gc_new`、`__c6_gc_store`、
  `__c6_gc_release`、`__c6_gc_live`、`__c6_gc_drain_budget`，
  每线程待释放队列 + 预算（默认 256）+ `draining` 重入标志；
- `tcc/c99-gen.c`：`c99_runtime_names[]`（:685）加全部新符号。

测试（`tests/gc.c`）：

- `new` + 作用域退出自动释放（`__c6_gc_live()` 归零）；
- `p = NULL`、双重 `p = NULL`；
- `p = p;`（自赋值净变化 0）；
- `return` 转移（对象在调用者手上存活）；
- `err()` 路径释放（错误传播后 `live()` 归零）；
- `defer` 里访问托管变量（声明后注册的 defer 先跑）；
- 未消费 FRESH / 重复消费的编译错误。

验收：三条命令全绿；`tests/c99/gc.c` 额外覆盖 C99 后端。

**结果**：`tests/gc.c` 10 个 `test` 块全过（原生 + C99 后端），
`tests/errors/gc.c` 4 条诊断全中，`make selftest` 40/40、
`BACKEND=c99 tests/run-tests.sh` 41/41、`make test-plain` 绿。

---

## P2 描述符、字段释放、数组 ✅

改动点：

- `struct_decl()`（:4867）收集字段 → 编译期元数据；
- TU 末尾 `c6_gc_flush()`：发 `__c6_gc_d_<tag>` 描述符（嵌入字段优先拓扑序），
  挂在 `tccgen_compile()`（:658）的 fixpoint 循环里；
- `new T[n]` 的元素数与 `count_flags`；元素类型描述符；
- 释放路径：`desc->fields` 逐字段释放（STRONG/WEAK/OWN/EMBED），
  长链/大数组走迭代避免递归爆栈；
- 按值规则：含托管字段的结构体禁止拷贝/传参/返回/进 tuple/进 slice（E-GC5）。

测试（`tests/gc_field.c`）：嵌套结构、`@T f[4]` 数组、`new T[n]`，
`@T f[n]` 的 `stride`，10 万节点链的释放不爆栈（`live()` 归零）。

验收：同 P1。

### 落地状态（与计划的差异）

- **不推迟到 TU 末尾**：`struct_decl()` 一收完字段就发射描述符（就地）。
  按值嵌入的结构体必须先完整定义才能当字段，所以被嵌入者的描述符一定
  已经发过 —— 拓扑排序整个不需要。`c6_gc_flush()` 与 fixpoint 钩子留给 P6
  （环检测需要看到晚到的 `static inline` 函数体）。
- **命名与链接**：`__c6_gc_f<N>` / `__c6_gc_d<N>`（每 TU 自增的计数器），
  并且是 `static`。同一头文件被多个 TU 包含时不会撞名，也不需要
  `external_sym()` 那套跨 TU 定义协商：描述符只被本 TU 的分配点和
  `hdr->desc` 用到。
- `new T[n]` 的元素数在 `count_flags` 里（P1 已有），释放时按
  `desc->size * i + payload` 逐元素走一遍描述符 —— 数组元素的字段也会被释放。
- 比计划多出来的硬错误（都是“不做就静默泄漏”的洞）：
  - union 含托管字段（E-GC6）；
  - 含托管字段的结构体在块内定义（描述符是文件作用域的静态数据）；
  - 柔性数组元素是托管引用（元素数不可知）；
  - 按值规则（E-GC5）落到四个点：声明、赋值、实参、返回值；
  - 新引用存进非托管槽（`struct T *p = new T;` 会泄漏）。
- 描述符的 `flags` 目前只填 `HAS_WEAK`；`drop` 指针还是 0（P4 接上）。
- tuple / slice / `memcpy` 的按值规则推迟到 P8 的统一审计里做。

**结果**：`tests/gc_field.c` 6 个 `test` 块全过（原生 + C99 后端），
`tests/errors/gc.c` 11 条诊断全中，`make selftest` 41/41、
`BACKEND=c99 tests/run-tests.sh` 42/42、`make test-plain` 绿。

---

## P3 `weak` ✅

改动点：

- 弱槽注册表：目标对象一条链表 + 全局自旋锁（冷路径）；
- 存储：值变化时重注册、`NULL` 解注册、绝不为 `NULL` 目标注册；
- 死亡顺序：置 `DEAD` → 同步失效全部弱槽（写 NULL）→ 析构 → 字段释放；
- 升级：`@T q = w;` → `__c6_gc_weak_upgrade`（原子 try-incref，失败得 NULL）；
- `w->f` 直接解引用 → 编译错误（必须升级）；
- **`async` 函数内 `weak` → 硬错误**（含形参与返回值），见设计 §9.2；
- `__c6_co_restore()` 加 `frame_addr` 校验（`t->frame_addr && t->frame_addr != base → trap`），
  `TccCoTask` 加字段（`lib/libtcc1.c` 与 `tcc/tccpp.c` 里的预定义字符串、
  `c99-rt.inc` 三处同步，注意 `__pad1` 的对齐）。

测试：

- `tests/weak.c`：升级成功/失败、目标死亡后槽变 NULL、弱字段与弱全局、
  多线程下的弱失效（P5 之后再加并发用例）；
- `tests/errors/async_weak.c`：`async` 体内 `weak` 局部 / `weak` 形参两条 `EXPECT`；
- `tests/errors/weak_deref.c`：直接解引用报错。

验收：同 P1。

### 落地状态（与计划的差异）

- **不要全局锁**：升级路径没有用「弱槽注册表 + 全局自旋锁」，而是对**目标对象的
  计数**做一次原子 CAS（`expected = rc; if (rc & DEAD) 放弃; CAS(rc, rc+1)`），
  失败就得到空值。这样 `weak` 的热路径（读）与 `shared` 一样是单对象原子操作，
  没有锁、没有跨线程争用；非原子对象走普通 `rc++`。设计 §11.3 的结论仍然成立。
  顺带解决「升级复活一个正在死亡的对象」：`DEAD` 位在清空弱槽**之前**置上，
  CAS 看到它就放弃。
- **升级即一次存储**：`@T q = w;` 编成
  `__c6_gc_weak_store(&q, &w, mode)`（一次调用完成 try-incref + 替换旧值 +
  释放旧值）。原因：分两步（先 incref 到临时，再 store）需要一个帧槽，而
  初始化表达式那条路径上没有可用的临时槽。
- 计划里的 `__c6_gc_weak_upgrade(dst, slot)` 改名为 `__c6_gc_weak_store`，
  返回新值，`tccpp.c` 预定义、`libtcc1.c`、`c99-rt.inc`、`c99-gen.c` 四处的
  名字一起改。
- 弱槽链表本身仍是**不设防**的（同线程内使用没有问题）：`shared` + 跨线程的
  弱失效要等 P5 的并发用例，届时链表操作要么加 per-object 锁、要么走无锁插入。
- `frame_addr` 校验做成**可选开关**（`-DC6_CO_CHECK_FRAME_ADDR`），因为实测
  现有 `co` 用例就会在不同地址恢复（见设计 §9.1）——它把静默损坏变成响亮失败，
  但只有排查时才打开。

- 顺带堵掉的两个洞：`return w;`（弱引用按值返回会丢注册、并把调用者拿到的那
  一次计数漏掉）→ 硬错误；`async` 函数的 `weak` 返回类型同理。

**结果**：`tests/weak.c` 6 个 `test` 块全过（原生 + C99 后端），
`tests/errors/async_weak.c` 3 条诊断全中，`tests/errors/gc.c` 12 条，
`make selftest` 43/43、`BACKEND=c99 tests/run-tests.sh` 44/44、`make test-plain` 绿。

---

## P4 析构 `drop` 方法

改动点：

- 方法识别处：名字是 `drop`、`void` 返回、恰好一个 `T*`/`@T` 参数 →
  登记为类型析构（每类型至多一个，重复报错）；
- 描述符增加 `drop` 指针，发射时用与 vtable 相同的 `(void(*)(void))Sym`
  手法（参考 `c6_emit_auto_impls()`，:9575）；
- 运行时 drain 步骤：`if (desc->drop) desc->drop(payload);`（在字段释放之前）；
- 静态检查：析构体内禁 `err`/`try`/`await`/`go`；禁止显式调用 `p->drop()`；
- 全局根：`@T` 全局登记到一张表，`atexit(__c6_gc_shutdown)` 释放一次。

测试（`tests/gc_drop.c`）：析构在最后释放时跑一次；析构里能读自己的托管字段；
析构顺序（析构 → 字段释放）；析构里释放另一个对象（重入）；析构在 `go` 线程跑；
析构没有为环运行（配合 P6 的 `c6_graph` 文档）；
`tests/errors/drop_bad.c`：返回值/参数个数/重复定义/`err` 内用/显式调用。

验收：同 P1。

---

## P5 `shared` 与跨帧/跨线程转移

改动点：

- `shared @T` 的存储/释放走 `__atomic_fetch_add`/`__atomic_sub_fetch`（`ACQ_REL`）；
  libtcc1.c 里自行 `#define __ATOMIC_ACQ_REL 4`（tcc 不预定义）；
- `go f(args)`：实参块里的托管引用 +1 进块、块释放时 −1
  （`__tcc_sp_args`/trampoline，`lib/libtcc1.c:789`/`800`）；
- `async`：参数进 `struct NAME_task` 时 +1，任务 slot 回收时 −1；
- 静态检查：`plain @T` 进 `go`/`async` → 错误（E-GC10）；
- 取消旧设计里“`go`/`async` 边界不能带托管引用”的限制。

测试（`tests/gc_thread.c`）：4 线程 × 25 万次引用转移，结束时 `live()` 归零；
`go` 传 `shared @T` 参数；`await` 传参；`-run` 下不依赖 libatomic。

验收：同 P1 + 原生与 C99 两种后端都要过。

---

## P6 静态环检测（硬错误）

改动点：

- `own @T`：独占字段/局部的存储与递归释放；`own` 值共享报错（E-GC13）；
- 类型图：节点 = 结构体，边 = STRONG 字段（含数组、EMBED）；
  `weak`/`own` 不建边；
- 单 TU 检查：`c6_gc_flush()` 里 DFS，带 `file:line` 路径；
  `tcc_error_noabort()` 列全部环后终止；
- 嵌入字段优先的拓扑序（描述符发射顺序）；
- 链接期检查：每 TU 发 `.c6.gcgraph` 段（`{from_hash,to_hash,kind}`，
  tag 名哈希），`tcc_output_file()` 与 `-run` 的内存链接阶段合并后跑 DFS；
- 文档：`-c99` 路径只有单 TU 检查。

测试：

- `tests/errors/gc_cycle_mutual.c`、`gc_cycle_self.c`、`gc_cycle_array.c`、
  `gc_cycle_embed.c`、`gc_cycle_cross_tu.c`（第二个 TU 用 `tests/tu/` 约定）；
- `tests/gc_own.c`：`own` 链/树的建与释放（`live()` 归零）、`own` 共享报错；
- 正例：`weak` 父指针、`own` 双向链、DAG（无环的互递归类型）不报错。

验收：同 P1。

---

## P7 async/go 集成与预算化 drain

改动点：

- `C6_CO_KIND_ABORT` + `__c6_co_abort()`；`__c6_co_destroy()` 先回卷（设计 §9.3）；
- body 恢复派发器加 ABORT 分支（强制跑清理）；
- 预算：队列消费在 `new`、作用域退出、协程调度步进处各消费一批；
- 协程调度器每步之后 `__c6_gc_drain_budget()`（让协作式任务池不被级联卡住）。

测试：

- `tests/async.c` 增：未 `await` 完就 `__c6_co_destroy` 的任务释放全部引用；
- `tests/gc_async.c`：`errdefer` 在 ABORT 回卷里执行；大数据在 async 体里分配释放；
- 1M 节点链一次性释放的延迟测试（放到 `examples/` 或 `tests/exit/`，
  断言不超时而不是断言具体毫秒）。

验收：同 P1。

---

## P8 文档与全量回归

- `README.md`：新增“托管引用（引用计数 GC）”一节（类型语义、`p = NULL`、`drop`、
  `weak`/`own`/`shared`、环检测报错、async 限制、与 C++ 智能指针对照、性能数字）；
  更新 `:932` 那句“没有隐藏状态也没有 GC”；
- `docs/grammar.md`：§7 静态约束（E-GC1..E-GC13）、§8 重写规则（存储/释放/
  `new` 降级）、§10 不支持形式、§11 实现位置；
- `docs/plan.md`：§11 实施状态表补阶段行；
- 回归：`make test-all`、`BACKEND=c99 tests/run-tests.sh`、`make test-plain`。

---

## 待拍板的决定

| 编号 | 决定 | 我的建议 |
|---|---|---|
| D1 | 原子计数的限定符名字：`shared` 还是保留 `atomic` | `shared`（描述权限而不是机制；`atomic` 会让人误以为 `p->f` 也是原子的）。若保留 `atomic`，设计文档全文替换即可 |
| D2 | 自环（`@Node next`）默认报错，通用图怎么办 | 默认报错 + 显式豁免 `struct Node c6_graph { ... }`（可 grep、可统计）；否则邻接表类结构无法表达 |
| D3 | 链接期跨 TU 环检查进 V1 还是 V2 | 进 V1（tcc 自己就是链接器，成本只有一次 DFS），否则跨 TU 环静默漏报 |
| D4 | 析构里禁止 `err`/`try`/`await`/`go` | 禁止（正在释放，失败无处传播） |
| D5 | 全局 `@T` 在进程退出时是否跑析构 | 跑（`atexit` 一次），否则 flush 类析构永远不执行 |
| D6 | drain 预算默认值 | 256（实测最坏 ~40 µs/次），可用 `-gc-drain-budget=N` 调整 |

---

## 风险登记

| 风险 | 影响 | 缓解 |
|---|---|---|
| 作用域退出点漏发射（`goto` 跨层、`err` 传播、`switch` 落空） | 漏释放/双重释放 | P1 的测试矩阵覆盖每种跳转；`-test` 里用 `live()` 断言归零 |
| 描述符发射与 `static inline` 体的 fixpoint 顺序 | 描述符缺失 → 运行期空指针 | 挂在现有 fixpoint 循环里（:658）；TI 式回归（inline 里 `new`） |
| async 帧搬动（既有缺陷） | 静默写坏别的栈 | 禁 async 内 `weak` + restore 地址校验 trap + 文档 |
| `own` 环（`b.next = a` 式构造） | 释放时死循环/越界 | `own` 赋值只接受 FRESH/`move()`；文档写明 |
| C99 后端漏符号 | C99 测试链接失败 | 每阶段检查 `c99_runtime_names[]` |
| `-std=c6` 之外的行为变化 | 破坏普通 C 回归 | `make test-plain` 作为每阶段门槛 |

---

## 现在的下一步

1. 确认 D1–D6；
2. 按 P0 → P8 顺序执行，每阶段结束跑三条验收命令并提交；
3. P0 只动类型系统，不产生任何运行期行为，最适合先落地。
