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
| P3 ✅ | `weak`（`async` 里的禁令后来随 A3 取消） | P2 | 帧搬动只能探测，不能 trap |
| P4 ✅ | 析构 `drop` 方法 | P3 | 全局根不能用 `atexit` |
| P5 ✅ | `shared` + 跨帧/跨线程转移 | P2 | 任务 slot 的归还点在 `__c6_co_destroy` |
| P6 ✅ | 静态环检测（单 TU + 链接期） | P2/P3 | 跨 TU 只在原生后端 |
| P7 ✅ | async/go 集成与预算化 drain | P1/P5 | 回卷检查点放在 `await` 恢复点 |
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
- 已落地（P7）：`async` 体里的托管局部量在回卷时的转移 —— `task` 句柄新增
  `t->start()`（`__c6_co_start`）把 body 推进到挂起点，作用域结束时
  `__c6_co_destroy` 回卷，defer 与托管局部量都按 §9.3 的派发器处理
  （`tests/async_frame.c` 覆盖）；同时修掉"挂起时把线程 defer 栈指针交给
  任务、调用者的清理被静默跳过"的既有 bug。
- 仍未做：`defer` 记录 8192 条静默丢弃（H3，P7 一起修）。

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
- `tests/async_frame.c`：帧住在任务里之后 `weak` 局部 / 形参 / 返回值在 `async`
  里都能用（原来那两条 `EXPECT` 随 A3 一起删掉了）；
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
  老模型下现有 `co` 用例就会在不同地址恢复（见设计 §9.1）——它把静默损坏变成
  响亮失败，但只有排查时才打开。A3（帧住在任务里）之后这条路径只剩 c67 会用。

- 顺带堵掉的两个洞：`return w;`（弱引用按值返回会丢注册、并把调用者拿到的那
  一次计数漏掉）→ 硬错误；`async` 函数的 `weak` 返回类型同理。

**结果**：`tests/weak.c` 6 个 `test` 块全过（原生 + C99 后端），
`tests/errors/gc.c` 12 条，
`make selftest` 43/43、`BACKEND=c99 tests/run-tests.sh` 44/44、`make test-plain` 绿。

---

## P4 ✅ 析构 `drop` 方法

结果：`drop` 在最后一次释放时跑一次（`tests/gc_drop.c` 6 个块），字段还在；
数组逐元素跑；`go` 线程里也跑；全局根在退出时释放（`tests/exit/drop_exit.c`）；
8 条诊断（`tests/errors/drop_bad.c`）。验收：`make selftest` 46 通过、
`BACKEND=c99 tests/run-tests.sh` 47 通过、`make test-plain` 绿。

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

落地状态（与计划的差异）：

- **全局根没用 `atexit`**：tcc 的 `atexit` 牵出 `__dso_handle`（`-run` 还有
  自己的 `atexit`）。改成每个含托管全局的 TU 发一对
  `__attribute__((constructor))` / `((destructor))`：构造时
  `__c6_gc_root(&g, kind, count, stride)` 登记，析构时
  `__c6_gc_shutdown()`（幂等）释放。`tests/exit/drop_exit.c` 用真实可执行
  文件验收（`-test` 模式不进 `runmain`，构造/析构不跑）。
- **描述符末尾发射**：`drop` 可能定义在结构体之后，所以描述符统一在 TU
  末尾发（`c6_gc_flush_descs()`），名字带文件名哈希
  `__c6_gc_d<fnv>_<n>`；`new T` 处只发 `extern` 声明。`&__c6_gc_dN` 的
  地址由链接期解析，跨 TU 各一份、不重名。
- **原型就够**：登记的钩在 `c6_method_register()` 里，所以头文件里的
  `void drop(struct Res *self);` 足以让别的 TU 的 `new Res` 拿到析构
  （已用两 TU 的例子验证）。
- **数组逐元素**：`new T[n]` 的析构跑 n 次，顺序是「元素析构 → 该元素
  字段释放」，逐元素循环（原来是一次析构 + 全部字段）。
- **`(void(*)(void *))` 的写法**和 vtable 一致；描述符按 `kind` 走
  `c6_gc_release_slot`，所以 `weak`/`own` 全局槽也能登记。
- 重复析构：同一 TU 里写两遍会先被 tcc 的 `redefinition of 'T_drop'`
  挡住，`'%s' already has a destructor` 只在两个不同符号时才会出现。
- 析构里 `try` 也挡住了（`expr catch` 和 `try { }` 两个入口）；
  `err()` 本来就要 `!error`，被 `'drop' cannot fail` 先挡。
- 没做的：`weak` 字段的析构时机（P3 已处理）、环（P6）。

验收：同 P1。

---

## P5 ✅ `shared` 与跨帧/跨线程转移

结果：`go` 的实参块和一个 `async` 任务各持有一份引用，两处都在**用完之后**
−1，所以「创建者先放手、线程/任务后跑完」是安全的；`shared @T` 的加减在
对象头里带原子标志，两个线程同时加减不丢更新（`tests/gc_thread.c` 4 个块，
连跑 10 次稳定）；任务还持有托管返回值（`tests/gc_async.c` 5 个块）；
6 条诊断（`tests/errors/gc_borrow.c`）。验收：`make selftest` 49 通过、
`BACKEND=c99 tests/run-tests.sh` 50 通过、`make test-plain` 绿。

改动点：

- `shared @T` 的存储/释放走 `__atomic_fetch_add`/`__atomic_sub_fetch`（`ACQ_REL`）；
  libtcc1.c 里自行 `#define __ATOMIC_ACQ_REL 4`（tcc 不预定义）；
- `go f(args)`：实参块里的托管引用 +1 进块、trampoline 返回前 −1
  （`c6_sp_emit()`，`__tcc_sp_args`，`lib/libtcc1.c:796`）；
- `async`：参数进 `struct NAME_task` 时 +1，任务被 `__c6_co_destroy()` 回收
  时 −1 —— 不在 body 里还，body 可能永远跑不完；
- 静态检查：`plain @T`/`own @T` 进 `go`/`async` → 错误（E-GC10）；
- 取消旧设计里“`go`/`async` 边界不能带托管引用”的限制。

落地状态（与计划的差异）：

- **任务 slot 的归还点**：`struct __c6_desc` 增加第 5 个成员
  `void (*release)(void *)`，编译器为每个带引用的 async 函数发一个
  `static void __c6_rel_NAME(void *)`，逐字段
  `__c6_gc_release(&t->pN / &t->val, mode)`；`__c6_co_destroy()` 在作废槽
  之前调用它。这样「body 正常结束」「body 被 abort」「任务从未被 step」
  三条路径都只在一个地方归还（比在每个 return 点发 −1 简单，也不会漏）。
- **形参是借用（新发现的既有 UAF）**：`void f(@Node n) { n = NULL; }` 会把
  调用者的引用减掉，静默释放一个还活着的对象（`live()` 归零但指针仍可用 ——
  今天就会写坏内存）。现在托管形参的槽打 `C6_GQ_BORROW` 标记（只存在
  `CType.gq` 缓存，不上 pointee；`vsetc()` 全清，形参由 `gen_function()` 在
  `sym_push_params()` 之后逐个数、body 的 async 形参在
  `c6_async_params_copy()` 里打），`vstore()` 的钩子见到就报 E-GC14。
- **`new shared T`（新发现的丢失更新）**：原子与否在对象头里
  （`h->meta & C6_GC_ATOMIC`），因为同一个对象可能同时被 `@T` 局部和
  `shared @T` 槽指着。`shared @T p = new T;` 拿到的是非原子对象，两个线程
  各 −1 只掉一次 —— 所以编译期拒掉（E-GC15），`new` 的结果用
  `SValue.gc_atomic` 记着。
- **任务结构体的字段带限定符**：`c6_async_type_tokens()` 对托管指针会发
  `shared @T`，所以 `t->pN`/`t->val` 是托管槽，调用点、body 的 `return`、
  awaiter 的读取都走同一个 store 钩子；`c6_async_propagate()` 写
  `TCC_ERR_SENTINEL` 时用 `c6_gc_no_hook`（哨兵不是引用）。
- **E-GC10 的检查点在声明处**（`c6_async_declare()`），形参和返回值一起查，
  一次报出；`weak` 先由 §9.2 的检查报（判定顺序：先 `weak` 再 `shared`，
  否则错误文案会被抢）。
- 测试没用「4 线程 × 25 万次」的压测：改成 4 个确定性用例（计数在每一步
  都对得上），压测只能发现「有时不对」，而且失败时看不出是哪一步。

测试（`tests/gc_thread.c`、`tests/gc_async.c`）：

- `go` 的实参块持有一份（线程里读到的计数 = 2）；启动者放手后对象仍活着；
  跨线程交接（存进 `shared` 全局，计数 = 2）；两个线程同时对同一对象加减，
  两次 +1 都算上（5）、退出后回到 1 —— 这条在 `new T`（非原子）时会稳定
  失败，正是 E-GC15 的实测依据；
- 任务前缀持有一份；创建者放手后任务仍活着；形参借用 + 拷进自有局部才 +1；
  托管返回值由 `val` 槽持有（`return n` 与 `return new shared T` 两条路径）；
  非托管参数不受影响；
- `tests/errors/gc_borrow.c`：借用形参赋值（普通/async 各一条）、`go` 传
  `plain @T`、`async` 形参/返回值非 `shared`、`shared` 槽装 `new T`。

验收：同 P1 + 原生与 C99 两种后端都要过。

---

## P6 ✅ 静态环检测（硬错误）

结果：类型图上有环就在编译期报错，带字段名和类型的定义位置
（`tests/errors/gc_cycle.c` 4 条：自环/互环/数组/按值嵌入）；`weak` 边和
`own` 边不算边，所以「父→子强、子→父弱」和无环互递归都照过。`own` 的
独占链/子树可以建起来并逐个释放（`tests/gc_own.c` 4 个块），`move(p)` 是
新增的内建表达式。验收：`make selftest` 51 通过、
`BACKEND=c99 tests/run-tests.sh` 52 通过、`make test-plain` 绿。

改动点：

- 类型图：节点 = 结构体，边 = 强引用字段（含 `@B f[N]` 数组、按值嵌入的
  结构体）；`weak`/`own` 不建边（`c6_gc_collect()` 记 `C6GcField.to_tag`）；
- 检查点：TU 末尾 `c6_gc_flush_descs()` 里三色 DFS（`mark` 0/1/2），命中回边
  报 E-GC1 并终止；
- `move(p)`：`__c6_gc_move(&p)` 返回指针、把槽清零、**不**释放，结果标 FRESH；
  这是 `own` 链/子树「先建后接」的唯一写法；
- `move(p)` 之后的空槽再用是编译错误（E-GC17）：`Sym.gc_moved` 在
  `c6_gc_move_expr()` 里置位、在 `c6_gc_store_hook()` 里清位，标识符压栈时由
  `c6_gc_moved_use()` 报错；分支各出口的状态用 `C6MoveSnap`（并行数组，
  `c6_move_snap` / `c6_move_restore` / `c6_move_merge` / `c6_move_branch_end`）
  在 `if`/`while`/`for`/`switch`/`foreach` 的汇合点取并集，`defer`/`errdefer`
  体在 `c6_defer_parse()` 里单独快照、`catch` 体在 `c6_catch_parse()` 里同理
  （它们分别在作用域退出、错误路径上才跑）；
- `own` 的存储规则照旧：只收 FRESH 或空值（E-GC13），共享就报错。

落地状态（与计划的差异）：

- **跨 TU 的链接期检查**：每个 TU 把边和类型名写进 `.c6.gcgraph` /
  `.c6.gcname` 两个段（名字的 FNV-1a 哈希当身份），
  `tcc_output_file()`（输出）和 `tcc_relocate()`（`-run`）在链接之后各跑一次
  DFS；段不存在就直接返回，普通 C 程序零成本。两个坑：记录大小必须是 8 的
  倍数（否则合并时对齐会在段中间塞空洞，第二条记录就错位），以及
  `tccelf.c` 里不能用 `tcc_error`（它的宏要 `s1` 且 `use_tcc_error_noabort`
  只在 `ONE_SOURCE` 里有），得用 `tcc_error_noabort` + `s1->nb_errors++`；
  `-c99` 那边依然没有（没有段、没有链接期），用例用 `// NATIVE-ONLY` 标；
- **`run-tests.sh` 的 errors/ 现在支持第二个 TU**：同名文件放
  `tests/tu/` 就会一起编译（`-run` 只接一个源文件，所以这种情况下走真正的
  编译+链接）；
- **只报第一条环**：`tcc_error_noabort()` 列全部的写法没做；
- **`own` 值不能从别的槽直接接**（只能 `new` 或 `move`）：`parent->next = kid`
  会被拒。这是对的（`own` 不计数，直接接会让两个所有者指向同一对象），
  但意味着**必须**有 `move`，所以 `move` 从“计划里的语法糖”变成 P6 的必做项；
- **`tests/gc_field.c` 的 `struct Node` 改成 `own @Node next`**：环检测一上，
  原来的 `@Node next` 自环就编译不过了 —— 那个测试本来就是建一条链，
  改成独占链语义不变（迭代释放的断言仍然过）；
- **嵌入环要前向声明**：`struct Wrap { struct Emb e; };` 与
  `struct Emb { @Wrap w; };` 互相引用时必须先 `struct Wrap;`，否则解析就失败
  （和 C 一样，不是环检测的问题）。

测试：

- `tests/errors/gc_cycle.c`：自环、互环、数组自环、按值嵌入环；
- `tests/errors/gc_move.c`：`move` 之后读/比较/传参/再 `move`/`return`，以及
  `if`/`while`/`switch` 只有一条路重新赋值（8 条），外加正常路径（重新赋值、
  `sizeof`、`defer` 体里的 move）；
- `tests/gc_own.c`：`new` 直接接链、`move` 接子树、`move` 后重新赋值再用、
  析构在独占链上逐个跑；
- 正例：`weak` 反向引用（要先升级才能解引用）、`own` 子树、无环互递归。

验收：同 P1。

---

## P7 ✅ async/go 集成与预算化 drain

结果：没跑完就被销毁的任务会**回卷** —— 恢复帧、跑 `errdefer` + `defer` +
托管引用的内联释放，再以 `C6_CO_KIND_ABORT` 结束，所以挂起的任务不会漏掉它
帧里的引用（`tests/gc_async.c` 的两个新块：`销毁挂起的任务会回卷`、
`回卷会跑 defer 与 errdefer`）。验收：`make selftest` 51 通过、
`BACKEND=c99 tests/run-tests.sh` 52 通过、`make test-plain` 绿。

改动点：

- `C6_CO_KIND_ABORT`（= 5）+ `__c6_co_abort(self)`：置 kind、清 `waiting`、
  `tcc_co_step_once()` 恢复并跑清理；`__c6_co_destroy()` 在作废槽之前调用它；
  `tcc_co_step_once()` 的 switch 接受 ABORT；
- 编译器：`c6_async_suspend()` 在**每个 `await` 的恢复点**（`c6_def_label()`
  之后）发 `if (__c6_co_kind(task) == 5) { <defer_begin(2,0)> finish(5) }`；
- `__c6_co_abort()` 对「从未启动」的任务直接返回（还没分配任何东西），对
  子任务递归回卷；
- 预算：不需要新机制 —— `c6_gc_drop()` 里每次释放最多处理
  `C6_GC_DRAIN_BUDGET`（256）个，级联自然摊平；`__c6_gc_drain_budget(n)`
  留着手工跑完（`tests/gc_field.c` 的长链用例在用）。

落地状态（与计划的差异）：

- **检查点从“末尾恢复派发器”挪到了“每个 `await` 的恢复点”**：第一版按计划
  放在 `c6_async_func_end()`，结果 `defer_state.enabled` 那时已经复位，
  `c6_defer_begin()` 是空操作 —— 生成的代码里只有 `__c6_co_finish(task, 5)`，
  清理体没进去，帧里的引用照样漏；把 `enabled` 掰回 1 也不行，
  `c6_defer_func_end()` 已经把跳转表 patch 完，事后补发会悬空。
  恢复点还在 body 中间，defer 机制活着、跳转表还没定稿，所以能正常发。
  代价是每个 `await` 多一次 `__c6_co_kind()` 调用；
- **1M 节点链的延迟测试没写**：断言“不超时”的测试在不同机器上不稳，
  改成靠 `tests/gc_field.c` 的 20000 长链 + 显式 drain 覆盖正确性；
- **C99 后端要同步**：`tcc/c99-rt.inc` 里同样要有 `C6_CO_KIND_ABORT`、
  `__c6_co_abort()`、`__c99_co_step_once()` 的 ABORT 分支和
  `__c6_co_destroy()` 的回卷调用 —— 少一处，`BACKEND=c99` 就会有用例失败
  （这次就是这么发现的）。

测试：`tests/gc_async.c` 新增两个块（帧里的引用还回来、`defer`/`errdefer`
各跑一次）。

验收：同 P1。

---

## P8 ✅ 文档与全量回归

结果：

- `README.md`：新增「托管引用（引用计数 GC）」一节（四种限定符的语义表、
  `p = NULL`、`move`、`drop`、环是编译错误、async/go 的限制、性能数字、
  与 C++ 智能指针的对照），并把「切片没有隐藏状态也没有 GC」那句改清楚
  （切片不是 GC 对象，托管引用是另一回事）；测试目录清单补上 7 个 gc 用例；
- `docs/grammar.md`：§7 静态约束补 C66–C77（环、借用、`own`/`shared` 的
  存储规则、跨帧/跨线程、`union`/块内定义/柔性数组、`memcpy`、析构、
  `move`）；新增 §8.1「托管引用的重写」（源码 → 生成代码对照表）；
  §10 未支持的写法补 7 条（显式调析构、`reset()`、跨边界传 `@T`、
  async 里的 `weak`、通用图、运行期环回收、自定义删除器）；§11 实现位置
  补 13 行；
- `docs/plan.md`：§11 实施状态表补 19–25 行（P0/P1、P2、P3、P4、P5、P6、P7）；
- `docs/gc-design.md` / `docs/gc-todo.md`：每个阶段的“落地状态（与计划的差异）”。

全量回归（本阶段最后一次）：`make test-all`（selftest 51 + tests2 全绿）、
`BACKEND=c99 tests/run-tests.sh` 52 通过、`make test-plain` 绿。

---

## 决定（D1–D6，已定）

| 编号 | 决定 | 结论 |
|---|---|---|
| D1 | 原子计数的限定符名字 | **`shared`**（描述权限而不是机制；`atomic` 会让人误以为 `p->f` 也是原子的），已按此实现 |
| D2 | 自环默认报错，通用图怎么办 | **默认报错**（已实现）；`struct Node c6_graph` 豁免**不做**，通用图只能写成 `own` 树 / `weak` 边（设计 §8.4） |
| D3 | 链接期跨 TU 环检查进 V1 还是 V2 | **进 V1**，已实现（`.c6.gcgraph` + 链接期 DFS，仅原生后端） |
| D4 | 析构里禁止 `err`/`try`/`await`/`go` | **禁止**，已实现（E-GC9） |
| D5 | 全局 `@T` 在进程退出时是否跑析构 | **跑**，已实现 —— 用 TU 末尾的 `__attribute__((destructor))` 调 `__c6_gc_shutdown()`（不用 `atexit`，tcc 的 `atexit` 会拖出 `__dso_handle`） |
| D6 | drain 预算默认值 | **256**（`C6_GC_DRAIN_BUDGET`，`lib/libtcc1.c:1071`）；**没有**做 `-gc-drain-budget=N` 命令行开关 —— 预算是运行期常量，要做开关得让编译器把它传给 libtcc1，暂不需要 |

---

## 风险登记

| 风险 | 影响 | 缓解 |
|---|---|---|
| 作用域退出点漏发射（`goto` 跨层、`err` 传播、`switch` 落空） | 漏释放/双重释放 | P1 的测试矩阵覆盖每种跳转；`-test` 里用 `live()` 断言归零 |
| 描述符发射与 `static inline` 体的 fixpoint 顺序 | 描述符缺失 → 运行期空指针 | 挂在现有 fixpoint 循环里（:658）；TI 式回归（inline 里 `new`） |
| async 帧搬动（既有缺陷） | 静默写坏别的栈 | 已修：帧住进任务（A3，五个后端 + C99）；c67 仍用拷贝模型，保留 `weak` 禁令与地址校验 |
| `own` 环（`b.next = a` 式构造） | 释放时死循环/越界 | `own` 赋值只接受 FRESH/`move()`；文档写明 |
| `move` 之后的使用漏报（全局槽、字段、`&p` 取过地址的槽不跟踪） | 运行期解引用空槽 | 只承诺跟踪本函数的自动托管槽（设计 §3.4 写明边界）；`tests/errors/gc_move.c` 覆盖读/比较/传参/再 `move`/分支合并 |
| C99 后端漏符号 | C99 测试链接失败 | 每阶段检查 `c99_runtime_names[]` |
| `-std=c6` 之外的行为变化 | 破坏普通 C 回归 | `make test-plain` 作为每阶段门槛 |
| 跨 TU 环在 `-c99` 上看不到 | 漏报 | 设计 §15 写明；用例 `// NATIVE-ONLY` 标注 |

---

## 收尾状态

P0–P8 全部完成，每阶段一个提交（`gc: Pn ...`），每个提交都带三条验收命令的
结果。**明确不做**的（都已写进 `docs/gc-design.md` §17）：

1. `-c99` 后端的跨 TU 环检查（没有段、没有链接期）；
2. `struct Node c6_graph` 通用图豁免（D2）；
3. 1M 节点链的“延迟不超时”测试（改成 20000 长链 + 显式 drain 的正确性覆盖）；
4. `-gc-drain-budget=N` 命令行开关（D6，预算目前是运行期常量 256）。

多环报告已经做了：单 TU 与链接期都用 `tcc_error_noabort` 把互不相交的环一次
列全（把环上节点标成完成再继续走），最后汇总终止。
