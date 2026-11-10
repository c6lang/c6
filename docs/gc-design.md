# c6 引用计数 GC 设计（`@T` / `weak` / `own` / `shared` / `drop`）

状态：设计定稿待编码。实施步骤见 [gc-todo.md](gc-todo.md)。
文中行号是当前 `main` 的快照，实施时以符号名为准。

---

## 1. 目标与非目标

目标：

1. 托管引用 `@T`：堆对象，引用计数，作用域退出或最后一个引用消失时确定性地释放。
2. **编译期**识别引用环，并且是**硬错误**（不是告警）：类型图中的环必须由程序员显式
   用 `weak`（反向引用）或 `own`（独占链）表达，否则编译不过。
3. 跨帧（`async`/`spawn`）和跨线程的所有权转移；原子计数由一个限定符显式选择。
4. 析构钩子 `drop` 方法（等价 C++ 析构函数 / Rust `Drop`）。
5. 不改变 c6 之外的任何行为；C99 后端（`-c99`）同样可用。

非目标（V1 明确不做）：

- 追踪式 GC、标记清除、分代；没有根扫描，因此**没有 stop-the-world**。
- 运行期环回收（只有 `c6_graph` 豁免类型才可能需要，见 §8.4，留 P3 之后）。
- 自定义分配器 / 自定义删除器（`drop` 不是删除器：内存永远由运行时 `free`）。
- 值语义的托管结构体（含托管字段的结构体**不能**按值拷贝）。
- 捕获式闭包：函数字面量 V1 是无捕获的裸函数指针；V2 的捕获式闭包已实现
  （`closure` 是托管引用，env 是一个 `@struct`，环检测走签名级保守边），
  见 [closure-design.md](closure-design.md) 与
  [closure-todo.md](closure-todo.md)。

---

## 2. 类型与限定符

```
@T              托管引用（强，非原子计数）
shared @T       托管引用，计数用原子操作，可跨线程
weak @T         弱引用（不持有；目标死亡时槽被置 NULL）
own @T          独占引用（无计数；随持有者一起释放；编译器保证不共享）
weak shared @T  跨线程的弱引用（弱槽失效也必须是原子/加锁的）
```

`@` 与 `weak`/`own`/`shared` 组合规则：

| 组合 | 合法 | 说明 |
|---|---|---|
| `@T` | ✓ | 默认 |
| `shared @T` | ✓ | 计数原子 |
| `weak @T` | ✓ | 单线程弱引用，注册/失效不加锁 |
| `weak shared @T` | ✓ | 弱槽注册/失效走原子路径 |
| `own @T` | ✓ | 只在字段/局部上出现，见 §3.4 |
| `weak own` / `own shared` | ✗ | 语义矛盾，报错 |

`@T` 的 ABI 就是裸指针（8 字节 / 4 字节），`sizeof(@T) == sizeof(void*)`，
可以和 C 互操作（借出规则见 §5）。

限定符是**软关键字**：按 `c6_soft_kw()`（`tcc/tccgen.c:13914`）的方式识别，
被同名 typedef 遮蔽时退回普通标识符，和 `co`/`spawn`/`task` 一致。

### 2.0 限定符存在哪里（P1 定稿，重要）

`CType` 只有 1 个字节的余量（`unsigned char gq`），而 `CType` 在 tcc 里
**到处按值复制**，还有大量 `CType t; t.t = ...;` 从不初始化 `gq` 的构造点。
如果“是不是托管引用”只由 `gq` 一位决定，那么任何没被初始化的字节都可能
凭空造出一个 `@T`：P1 实现期间它就真的让 `char[:]` 的 `data` 字段看起来
像 `own shared @char`，并让整个 `slice` 测试段错。

所以托管性质有**两份**，只有一份可信：

| 位置 | 内容 | 谁写 | 可信 |
|---|---|---|---|
| `CType.gq` | 限定符（方便复制/打印的副本） | `parse_btype()` 的 `@` 分支 | ✗ 仅用作缓存 |
| `type->ref->type.gq`（被指符号） | `C6_GQ_MANAGED \| 限定符` | 仅 `parse_btype()` / `new` | ✓ **唯一依据** |

- `mk_pointer()` 与 `c6_global_pointer()` 建一个被指符号时，把它的
  `type.gq` 显式清 0 —— 于是“普通指针”的被指符号永远不带托管位，
  `sym_push()` 也只把**校验过的**副本（`c6_type_gq()`）写进符号。
- 判定统一走 `c6_type_gq()` / `c6_is_managed()`：`(t & VT_BTYPE) == VT_PTR
  && t->ref && (t->ref->type.gq & C6_GQ_MANAGED)`，垃圾字节无法伪造。
- 想把一个托管类型“当成裸指针”处理（编译器内部的返回槽、临时槽、释放槽），
  用 `c6_gc_plain()`：它 `mk_pointer()` 出一个全新的、标记为 0 的被指符号。
  **不要再写 `t.gq = 0`** —— 那样副本清了、可信副本还在，`vstore()` 的钩子
  照样会把它当托管槽（P1 踩过：`return p;` 的返回槽被当成普通赋值，
  于是拿未初始化的旧值去 decref，堆当场烂掉）。

### 2.1 语义矩阵

| 操作 \ 类型 | `@T` | `shared @T` | `weak @T` | `own @T` |
|---|---|---|---|---|
| `new T` / `new T[n]` 的结果 | FRESH | 要写 `new shared T`（E-GC15） | ✗ | FRESH |
| `p = NULL`（释放） | ✓ | ✓（原子） | ✓（解注册） | ✓（释放整条链） |
| 同类型赋值 | +1/−1 | 原子 +1/−1 | 重注册 | 转移（旧链释放） |
| 存进 `@T` 字段 | ✓ | ✗（字段必须是 `shared`） | ✓ | ✓（字段必须是 `own`） |
| 传给普通函数形参 | 借出 | ✗（形参需 `shared @T`） | 只读 → 需先升级 | 借出 |
| `return p` | 转移 | 转移 | ✗ | 转移 |
| 进 `spawn` / `async` 帧 | ✗ 编译错误 | ✓（+1 进帧，−1 出帧） | ✗ | ✗ |
| 解引用 `p->f` | ✓ | ✓ | ✗ 必须先升级 | ✓ |
| `p == NULL` / `!p` | ✓ | ✓ | ✓ | ✓ |
| 含它的对象做 `memcpy` | ✗ 报错 | ✗ 报错 | ✗ 报错 | ✗ 报错 |
| 对象死亡时 | 析构 → 字段释放 → `free` | 同，可能发生在任意线程 | 槽置 NULL | 递归释放链 |

`weak` 不能直接解引用，必须升级（`@T q = w;` = 原子 try-incref），否则
`w->f` 是编译错误。这样弱引用的读竞争在语言层面被消灭。

---

## 3. 对象布局与描述符

### 3.1 头部（32 字节）

```c
typedef struct C6GcHdr {
    unsigned long rc;          /* 引用计数（plain 或原子，由分配时的 desc->flags 决定） */
    unsigned long count_flags; /* 低 63 位：new T[n] 的元素数；最高位：DEAD */
    const struct __c6_gc_desc *desc;
    union {
        struct C6GcWeak *weak; /* 弱槽链表头（仅 desc->flags & HAS_WEAK） */
        struct C6GcHdr  *next; /* 待释放队列；死亡时弱槽已处理完，可复用此字段 */
    } u;
} C6GcHdr;
#define C6_GC_DEAD (1UL << 63)
```

载荷紧跟头部，`offsetof(payload) == 32`，满足 16 字节对齐（`long double`/SSE）。
没有独立控制块（对比 `std::shared_ptr` 的 16 字节值 + 单独分配的控制块）。

### 3.2 类型描述符（每个 TU 末尾发射，静态数据）

```c
enum { C6_GCK_STRONG = 1, C6_GCK_WEAK = 2, C6_GCK_OWN = 3, C6_GCK_EMBED = 4 };

struct __c6_gc_field {
    unsigned off;                    /* 载荷内偏移 */
    unsigned kind;                   /* 上面枚举 */
    unsigned count;                  /* 数组元素数，标量 = 1 */
    unsigned stride;                 /* 元素跨距 */
    const struct __c6_gc_desc *sub;  /* EMBED 时的子描述符 */
};

struct __c6_gc_desc {
    unsigned size, align;
    unsigned nfields;
    unsigned flags;                  /* HAS_WEAK / ATOMIC / GRAPH */
    void (*drop)(void *);            /* 析构方法，可为 0 */
    const struct __c6_gc_field *fields;
};
```

**落地方式（P2）**：命名 `__c6_gc_f<N>` / `__c6_gc_d<N>`，`N` 是每 TU 自增的
计数器，两个对象都是 `static`。发射用 `TokenString` 拼声明 + `c6_decl_tokens()`
（与 `interface` 的 vtable 同一手法，`c6_emit_auto_impls()`）。

- **就地发射**：`struct_decl()` 一收完字段（`struct_layout()` 之后）就发描述符。
  按值嵌入的结构体必须先完整定义才能当字段，被嵌入者的描述符一定已经发过，
  所以**不需要拓扑排序**。
- **每 TU 一份**：`static` 让同一头文件被多个 TU 包含时不撞名；描述符只被本 TU
  的分配点（`new T`）和对象头（`hdr->desc`）用到，不需要跨 TU 协商定义。
- 代价：同一类型在 N 个 TU 里各有一份描述符（每个字段 20 字节），换来的是
  没有链接期的定义协商、也没有“谁来发”的规则。

### 3.3 描述符的收集点

- `struct_decl()`（`tcc/tccgen.c`，`struct_layout()` 之后）调 `c6_gc_collect()`：
  逐字段用 `c6_gc_elem()` 剥掉数组层（记下元素数与跨距），按限定符定
  `kind`（STRONG/WEAK/OWN），嵌入结构体则定 EMBED 并记下子 tag；
- union 含托管字段 → 报错（union 的活跃成员不可静态判定）；
- 柔性数组成员是托管引用 → 报错（`new T[n]` 才是数组的写法）；
- 含托管字段的结构体在**块内**定义 → 报错（描述符是文件作用域的静态数据）；
- 含托管字段的结构体禁止按值拷贝/按值传参/按值返回（§5）。

没有托管字段的结构体**不发描述符**（`new T` 的第三个实参就是 0）。

### 3.4 `own` 的语义

`own @T` 是**独占**所有权，不维护计数：

- 赋值 `f = new T` 或 `f = move(other)` 时，先释放 `f` 原来的整条链，再接管新值；

**落地（P6）**：`move(p)` 是内建的表达式，读作「把 `p` 槽里的那一份交出来」：
生成 `__c6_gc_move(&p)`（返回指针并把槽清零、**不**释放），结果标记成 FRESH，
所以能存进 `own @T` 或任何托管槽；`p` 的槽变空，作用域退出时无事可做。
它同时对 `@T` 局部有效（等价于「把计数转走」），这是「先建子树、再接到父节点」
的唯一写法 —— 直接写 `parent->next = kid;` 会被拒（`own` 只收 FRESH，E-GC13）。
- 持有者死亡时递归释放 `own` 字段（迭代实现，不用递归栈）；
- `own` 边**不进入引用计数的类型图**（它不产生计数环），因此不参与 §8 的环检测；
- `own` 值不能共享：赋值源必须是 FRESH 或显式 `move()`，否则报错。

**编译期检查（E-GC17）**：槽既然空了，在重新赋值之前再用它就是编译错误 ——
读值、比较、解引用成员、传参、`return`、再 `move` 都算；赋值（含 `p = NULL`）
解除。检查只在编译期成立，不生成任何代码，C99 后端同样生效。

- 只跟踪**本函数的自动托管槽**：全局（要整程序知识）、字段与数组元素（写入口
  在编译器的视野之外）、`weak` 槽（它不是计数引用）都不跟踪；`&p` 取过地址的
  槽也不跟踪（指针可能从别处写回来，跟踪会漏报/误报）；
- 控制流按「可能已交出」合并：`if`/`else`、`while`/`for`/`do`、`switch`
  （`break`、轮空、落空各算一条路）、`foreach` 都取各条路出口状态的并集，
  因此只有「每条路都重新赋值」才算安全；走不到的出口（`return`/`break`/
  `continue`/`goto` 之后）不参与合并；
- `defer`/`errdefer` 体（以及 `catch` 体）单独分析：它们在作用域退出或错误
  路径上才跑，`defer { move(p); }` 不影响后面的直线代码；
- `sizeof(p)`/`typeof(p)` 不读值，照过；不生成代码的死代码不检查；
- 赋值目标是裸标识符才算「重新赋值」：`(p) = x` 这种把目标括起来的写法仍按
  「使用」报错（很少见，写成 `p = x` 即可）。

这给了“链表/树”一个无需 `weak` 的合法写法：

```c
struct Node { int v; own @Node next; };        /* 独有链：无环、无计数 */
struct Tree { int v; own @Tree left, right; }; /* 独有子树 */
struct DNode { int v; own @Node next; weak @DNode prev; };  /* 双向链 */
```

---

## 4. 运行时 API 与不变量

### 4.1 导出符号（`lib/libtcc1.c` + `tcc/c99-rt.inc`）

```c
void *__c6_gc_new(unsigned long size, unsigned long count,
                  const struct __c6_gc_desc *d, int flags); /* 清零；失败 trap */
void *__c6_gc_store(void **slot, void *nv, int mode); /* 强/原子/弱存储 */
void *__c6_gc_own_store(void **slot, void *nv, int mode); /* own 字段/局部 */
void  __c6_gc_release(void **slot, int mode);         /* 作用域退出：置 NULL 并释放 */
void *__c6_gc_incref(void *p);                        /* 借出/转移时补一次计数 */
void *__c6_gc_drop(void *p);                          /* 丢掉裸值，返回 NULL */
void *__c6_gc_weak_store(void **dst, void **weak_slot, int mode); /* 升级 */
unsigned long long __c6_gc_live(void);                /* 活对象数（测试用） */
unsigned long long __c6_gc_refcount(void *p);         /* 测试用 */
void  __c6_gc_drain_budget(int n);
void  __c6_gc_root(void *block, int kind, unsigned long count,
                   unsigned long stride);             /* 登记一个托管全局槽 */
void  __c6_gc_shutdown(void);                         /* 退出时释放全局根 */
```

存储类返回它的 `nv`，这样生成的代码手里始终有一个值可以 `vpop()`。

`mode` 位：

| 位 | 值 | 含义 |
|---|---|---|
| `C6_GC_FRESH` | 1 | 值是刚造出来的：接管，不再 incref |
| `C6_GC_ATOMIC` | 2 | 计数走原子操作（`shared`） |
| `C6_GC_WEAK` | 4 | 目标是弱槽：注册/解注册，不计数 |
| `C6_GC_OWN` | 8 | 目标是 `own`：旧值直接销毁，不 decref |
| `C6_GC_INIT` | 16 | 槽**从未写过**：旧值视为空，不许读它 |

`C6_GC_INIT` 是 P1 补上的：局部量和 `new` 出来的对象都带着未初始化的槽，
读出来的旧指针去 decref 等于随机改堆。初始化表达式（`init_putv`）和
`own` 的存储都带这一位；`own` 的存储是**唯一一次**写槽（它替换了普通
`vstore()`），所以它的 `old == nv` 短路也必须被 `INIT` 关掉。

### 4.2 硬性不变量

1. **存储顺序**：`store(slot, nv)` 必须**先 incref(nv) 再 decref(*slot) 再写槽**。
   `p->next = p->next->next;` 依赖这一点，否则先 decref 会释放读到旧值的那张表项。
2. **NULL 合法**：`store(slot, NULL)` 只做 decref + 写 NULL；`slot` 为弱槽时是解注册。
3. **死亡顺序**：`rc` 减到 0 → 置 `DEAD` → **同步**失效所有弱槽 → 调用 `desc->drop` →
   释放字段 → 入待释放队列 → `free`。弱槽失效必须同步，不能推迟到队列，
   否则弱升级可以复活一个正在死亡的对象。
4. **重入**：析构或字段释放里可能再次进入 `decref`；用线程本地 `draining` 标志，
   嵌套调用只入队不递归，队列由最外层循环消费（栈深有界）。
5. **原子路径**：`shared` 用 `__atomic_fetch_add` / `__atomic_sub_fetch`，内存序 `ACQ_REL`。
   **不用任何全局锁，也不用 per-object 锁**（见 §11.3）。
6. **弱槽注册**：每个目标对象一条链表；注册/注销/失效走一把全局自旋锁（冷路径），
   不在引用计数热路径上。
7. **待释放队列**：**每线程**一份，谁最后 decref 谁消费 → 没有跨线程队列，没有全局锁。
8. **不读未初始化的槽**：`store`/`own_store` 只在没有 `C6_GC_INIT` 时才读
   `*slot` 当旧值；带了 `INIT` 就当作空槽，而且**必须**真的写槽（`own` 的
   存储是唯一一次写，短路返回会把槽留成垃圾）。
9. **预算**：队列每次最多消费 `budget`（默认 256）个对象，剩余留给下一次
   `__c6_gc_drain_budget()` / 作用域退出 / `new` / 协程调度步进，把级联延迟切成小片。

### 4.3 没有 STW

没有根扫描/标记/清扫，因此**不存在全局暂停**。唯一的“暂停”是级联释放：
一次 `decref` 把整条链打成 0 时释放 N 个对象。实测口径见 §11.4
（1M 节点链 12 ms），预算化之后单次调用最坏 ~40 µs。

---

## 5. 所有权分析

`SValue` 增加一个 3 态字段 `gc_state`：

- `GC_BORROW`：借来的，不拥有 → 可以随便用，不能 `return`/存进托管槽；
- `GC_FRESH`：`new` 产生的、或从托管槽**移出**的；必须被消费恰好一次；
- `GC_OWNED`：从托管槽读出（`p->f`、`*pp`）→ 是借出，写回才算拥有。

合法消费点（编译器在这里生成 +1/−1 或释放）：

1. 局部/成员/元素初始化（`init_putv`，`tcc/tccgen.c:12107`，动态分支在 `:12244`）；
2. 赋值（`expr_eq` → `vstore`，`tcc/tccgen.c:4096`）；
3. `return`（转移给调用者的值）；
4. `(void)expr` 或语句表达式丢弃（FRESH 直接释放）；
5. 函数实参：普通形参是**借出**，FRESH 实参在调用后由调用点释放
   （生成隐藏临时 + 调用后释放）。

任何其它位置的 FRESH（`?:` 合并、`&&`/`||` 内部、条件上下文、`sizeof`）
一律报错。

借出规则：`@T` → `T*`/`void*` 只允许**立即使用**，不允许保存：

- 保存（`T *p = managed;`、`s->p = managed;`、`return (void*)managed;`）→ 错误；
- 实现：`SValue` 上加 `gc_src` 溯源位，经强转/算术传播，在所有存储点检查；
- 传给裸指针形参 → 警告 `-Wgc-borrow`（默认开，可 `-Werror`）；
  形参声明 `__attribute__((c6_borrow))` 时降级为静默（表示“被调者不会保存”）。

**形参是借用（P5 定稿）**：托管形参的槽由调用者（`spawn`/`async` 时由实参块或
任务前缀）拥有，被调者不计数、不释放。因此形参槽打上 `C6_GQ_BORROW`
（只存在 `CType.gq` 缓存里，绝不上 pointee），`vstore()` 的钩子见到它就报
E-GC14 —— 否则 `void f(@Node n) { n = NULL; }` 会把调用者的引用减掉，
静默释放一个还活着的对象（`tests/errors/gc_borrow.c6`）。要改写就先拷进
自有局部：`@Node m = n;`（+1，退出时 −1）。

按值规则：含托管字段的结构体不能按值拷贝、按值传参、按值返回、进 tuple、
存进 slice；`memcpy`/`memmove` 实参指向这类类型时（`TOK_memcpy` 是独立记号，
`tcc/tcctok.h:224`）报错；`memset` 警告（会漏引用，不是 UAF）。

**落地方式（P2）**：一张表 `c6_gc_value_check()`，四个检查点 ——
`decl()` 里声明对象、`vstore()` 里 `VT_STRUCT` 赋值、`gfunc_param_typed()`
里每个实参、`c6_gc_return_hook()` 里返回值。检查本身是“剥掉数组层后
是不是含托管字段的结构体”，命中就报 E-GC5。tuple/slice/`memcpy` 三个点
留给 P8 的统一审计。

同一个函数还负责“新引用存进非托管槽”这个泄漏点（E-GC6d）：`struct T *p =
new T;` 的 FRESH 值没有第二个所有者，存进普通指针就再也没人释放。

---

## 6. 作用域释放：内联，不复用 defer 派发

**决定：托管引用的释放代码直接内联在作用域退出点，不注册清理点。**

理由（两条，都是硬的）：

1. **正确性**：派发型清理要在派发记录里存槽地址。`async` 体在 A3 之前每次恢复
   都把帧拷到**新的**栈地址（§9.1），存下来的地址恢复后就是悬垂地址；内联代码
   每次都从 `fp` 重算地址，天然安全。（A3 之后帧住进任务、地址在任务一生里不变，
   但内联这条理由仍然成立且更省。）
2. **性能**：实测（§11.4）派发型 10.7 ns/引用 vs 内联 2.5 ns/引用，4.3 倍。

发射点：块结束（`prev_scope_s`）、`return`、`break`/`continue` 跳出、
`goto` 跨出（沿作用域栈逐层发射）、`err()` 传播路径。
顺序：逆声明序。

用户写的 `defer`/`errdefer` 现在**也不需要运行期记录**了：清理点是编译期常量，
"哪些已经注册"是函数帧里的一块位图，"正在服务哪个请求"是两个 `int`
（语义见 [grammar.md](grammar.md) 8.2）。于是 H3（记录栈满 8192 静默丢）自然
消失——没有记录栈可满，上限变成一个编译期数字（一个函数 128 个清理点，超了
直接报错）。这条也顺带让 `defer` 在 ISR、`spawn` 线程、递归帧里都不需要任何
线程级状态。

---

## 7. 析构：`drop` 方法

```c
struct File { int fd; };
int close_file(struct File *self) { ... }     /* 普通方法照旧 */
void drop(struct File *self) { close(self->fd); }   /* ← 名字叫 drop 的方法 = 析构 */
```

识别规则与现有方法一致（第一个参数是指向结构体的指针、文件作用域、
`tcc/README.md:310` 起的规则），额外要求：返回值 `void`、恰好一个参数、每类型至多一个。

- 调用时机：`rc` 减到 0、弱槽已失效之后，字段释放之前；
- `self` 是**借用**（不 incref），保证在析构体内有效；
- 析构体内**禁止**：`err`/`try`/`await`/`spawn`（正在释放，失败无处传播）；
- 析构体**不能复活**自己：`self` 是 borrow，存进任何托管槽都是 §5 的溯源错误，
  这条规则自动生效；
- 析构方法**不能显式调用**（`p->drop()` / `drop(p)` → 错误）；提前释放用 `p = NULL`；
- 析构指针放在描述符里 → 跨 `interface` 也是正确的析构（等价虚析构，但不需要 vtable）；
- 析构可能在**任意线程**执行（最后释放它的那个线程），文档必须写明；
- 全局 `@T g` 在进程退出时释放一次 → 析构会跑（见下面“落地方式”）。

### 7.1 落地方式（与上面计划的差异）

1. **析构指针在描述符里**：`struct __c6_gc_desc` 的第 5 个字段是
   `void (*drop)(void *)`，编译器把 `drop` 的符号写成 `(void (*)(void *))Res_drop`
   ——和 vtable 对象同一套写法。描述符本身在**翻译单元末尾**发射
   （`c6_gc_flush_descs()`），所以 `drop` 定义在结构体后面也不要紧；
   描述符的名字带**文件名哈希**（`__c6_gc_d<fnv>_<n>`），跨 TU 各算一份、
   不会重名，`new T` 处只发一个 `extern` 声明把地址留给定义。
2. **识别时机**：`c6_method_register()` 里做，所以**原型**就够了 ——
   头文件里声明 `void drop(struct Res *self);`，别的 TU 里 `new Res`
   同样能拿到析构（已用两 TU 的例子验证）。
3. **数组**：`new T[n]` 的析构跑 **n 次**（每个元素一次，和 C++ 一样），
   顺序是「元素析构 → 该元素的字段释放」，逐元素循环。
4. **全局根不用 `atexit`**：tcc 的 `atexit` 会牵出 `__dso_handle`，
   而 `-run` 又是自己的 `atexit`。改成编译器为每个含托管全局的 TU 发一对
   `__attribute__((constructor))` / `__attribute__((destructor))` 函数：
   构造时 `__c6_gc_root(&g, kind, count, stride)` 把槽登记进运行时的表，
   析构时调用 `__c6_gc_shutdown()`（幂等）释放。`-run`、可执行文件、
   c99 后端三条路都验证过（`tests/exit/drop_exit.c6`）。
5. **静态检查**：`int drop(...)` → `'drop' must return void: ...`；
   `void!error drop(...)` → `'drop' cannot fail ('!error'): ...`；
   体内 `try`/`catch`/`spawn` → 各自报错（`err()` 本来就要求 `!error`，
   所以顺带被第一条挡住）；显式调用 → `a destructor cannot be called: ...`。
   每个类型只能有一个析构；同一 TU 里写两遍会先被“重复定义”挡住。
6. **不能复活自己**：`self` 是 `struct T *`（借用），`@T q = self;` 落在
   §5 的溯源规则上自动报错，不需要额外的检查。
7. **`-test` 模式不跑构造/析构**（`tcc_run_tests()` 不进 `runmain`），
   所以全局根的验收放在 `tests/exit/` 里用真实可执行文件跑。

对比 C++：`drop` 是**所有权触发**，不是作用域触发。作用域触发的清理仍然用 `defer`。
两者分工明确：`defer` = RAII（作用域），`drop` = Rust `Drop`（最后引用）。

---

## 8. 静态环检测：硬错误

### 8.1 图与规则

- 节点：结构体类型（union 不允许含托管字段，故无 union 节点）；
- 边：`@B` 字段（含 `@B f[N]` 数组、含按值嵌入的结构体）；
- `weak` 边：**不建边**（不持有）；
- `own` 边：**不建边**（独占，不产生计数环，见 §3.4）；
- 环（自环或多节点环）→ **编译错误**；
- 修法只有两个：把某条边改成 `weak`（反向引用）或 `own`（独占链）。

```c
struct A { @B b; };          /* error: reference cycle A -> B -> A */
struct B { @A a; };
/* 修法 1（反向引用）*/ struct B { weak @A owner; };
/* 修法 2（独占子树）*/ struct A { own @B b; };

struct Node { @Node next; };            /* error: 自环 */
struct Node { own @Node next; };        /* ✓ 独有链 */
struct Node { @Node next; weak @Node prev; };  /* ✓ 双向链 */
```

这条规则强制了“父→子强、子→父弱”的正确设计，代价是**共享边的通用图**
（邻接表那种“边可以复用”的图）无法表达，见 §8.4。

### 8.2 检查点

1. **单 TU（已实现）**：TU 末尾（`c6_gc_flush()`，挂在 `tcc/tccgen.c:658` 起的
   `for(;;){ gen_inline_functions(); if (!c6_emit_auto_impls()) break; }` 的
   fixpoint 循环里，保证 `static inline` 体里出现的 `new` 也被覆盖）。
   诊断为错误，带完整路径与 `file:line`。
2. **跨 TU（已实现，仅本机后端）**：每个 TU 把边和类型名写进两个段 ——
   `.c6.gcgraph`（`{unsigned from, to}`，8 字节一条）与 `.c6.gcname`
   （`{unsigned hash, unsigned pad, char name[40]}`，48 字节一条，记录大小取
   8 的倍数，这样合并时不会因为对齐在段中间插入空洞）。身份是**类型名的
   FNV-1a 哈希**，每个 TU 各自算，所以不依赖任何跨 TU 的编号。
   tcc 自己就是链接器：`tcc_output_file()`（输出可执行/`.o`）与
   `tcc_relocate()`（`-run`）在链接之后各调一次 `c6_gc_graph_check()`
   （`tcc/tccelf.c`，段不存在就直接返回，普通 C 程序零成本）。
3. **不完整类型**：TU 内 `@B` 而 `B` 只有前向声明时，该边只记录不判定；
   由链接期给出结论。（若该类定义在整次链接里始终没出现，那条边没有对端
   类型可走，链接期既不报环也不报“未定义” —— 类型名以哈希为身份，没有
   独立的“未定义类型”检查。）

诊断示例（错误，不是告警）：

```
error: reference cycle: A -b-> B -a-> A; break it with 'weak' or 'own' (type 'A' at a.c:3)
error: reference cycle across translation units: A -> B -> A; break it with 'weak' or 'own'
```

**落地（P6）**：单 TU 的 DFS 在 `c6_gc_flush_descs()` 里跑（`tcc/tccgen.c`），
`mark` 三色（0 未访问 / 1 在栈上 / 2 完成），命中回边用 `tcc_error_noabort`
报出来，然后把环上的节点标成完成再继续走 —— 所以**互不相交的环会一次列全**，
最后再用 `tcc_error` 汇总终止（链接期那一侧同样处理）。
实际文案带字段名与类型的定义位置：

**路径长度没有上限**：两处检查（单 TU / 链接期）都是**迭代** DFS，路径栈与
诊断文案都按需 `tcc_realloc` 增长，所以一条合法无环长链（`A0 -> A1 -> … ->
A40`）永远只受类型数本身限制。这条是不变量，不是巧合：早期版本用 32 个槽的
定长路径，**第 33 个类型**就被误报成 `reference cycle`（诊断里没有重复节点，
正是误判的痕迹），回归用例见 `tests/gc_chain.c6`（40 层链）。

```
error: reference cycle: A -b-> B -a-> A; break it with 'weak' or 'own'
       (type 'A' at a.c:3)
```

字段名与类型名都来自 `C6GcField.name_tok` / `.to_tag`（边在
`c6_gc_collect()` 里记录，`to_tag` 只在目标结构体完整时才解析，所以前向
声明的边不会误报）。

### 8.3 误报面

- 类型图有环但运行期从不两边都赋值 → 仍然报错（保守但正确：环是**可能**的，
  引用计数无法回收）；
- 头文件被多个 TU 包含 → 单 TU 检查会在每个 TU 各报一次；链接期检查去重。

### 8.4 与“通用图”的冲突（未决，见 todo D3）

共享边的图（`struct Node { @Node edges[4]; }`）是合法的无环数据结构，但类型图自环
→ 被禁。若必须支持，唯一自洽的出口是**显式豁免**：`struct Node c6_graph { ... }`
（可 grep、可 review、可统计），并对这些类型提供运行期环回收（P3 之后）。
在豁免引入之前，通用图只能写成 `own`（独占树）或 `weak`（不持有边）。

---

## 9. `async` / `spawn` 集成

### 9.1 帧住在任务里（P7/A3，已实现）

`async` body 的帧不再在调用者的栈上：`__c6_co_new()` 一次 `tcc_th_alloc` 出
`struct NAME_task` + 帧（帧里带着自己的清理状态），运行期把帧顶作为第二个参数交给
body（`int __c6_body_f(void *task, void *frame)`），后端把帧寄存器直接指向它
（x86_64/i386 `rbp`、arm64 `x29`、riscv64 `s0`、arm `fp`、C99 后端
`__c99_frmp`）。于是：

- 挂起**不拷贝任何东西**：`__c6_co_suspend` 只把错误槽存进任务——帧就在任务
  里，没有可搬的东西；
- 帧的**绝对地址**在任务的一生里不变，`&local` 跨过 `await` 依旧有效
  （`tests/async_frame.c6` 用 `co` + `__c6_co_run()` 恢复来断言——那正是老模型
  会搬帧的组合）；
- 帧没有 4 KiB 上限，任务也没有固定池上限（每个任务一块，结束即释放）。

后端用 `c6_backend_frame_in_task()` 起手回答能不能做到，做不到的直接在编译期
拒绝 `async`；收尾时用 `c6_frame_size_set()` 把帧大小回填进描述符的第 6 个
成员。回答 1 的有：
x86_64（ELF/PE/mach-o，PE 另有专门的 `.pdata`/`.xdata` 记录）、i386
（ELF/PE）、arm64（ELF/PE/mach-o）、arm（EABI/VFP/FPA/WinCE）、riscv64、
C99 输出后端。

仍然存在的限制：

1. **c67** 回答 0：它没法把帧寄存器指向任务内存，所以这个目标上 `async` 是
   编译期错误（`'async' is not supported on this target`）——不放行"帧会被搬
   动"的半套语义，拷贝模型（`C6_CO_COPY_FRAME`、`frame_addr` 校验）已删除；
2. `-b`（边界检查）与 `async` 互斥：`'-b' cannot be combined with 'async'`；

### 9.2 `weak` 与 `async`（已放开）

帧住在任务里之后，弱槽（它是指向槽本身的指针）在整个任务期间地址不变，
所以 `weak` 局部、`weak` 形参、`weak` 返回值都可以出现在 `async` 函数里
（`tests/async_frame.c6` 覆盖这三种）。

```c
async int f(int fd, weak @Node param)   /* ✓ 形参 */
{
    weak @Node w = ...;                 /* ✓ 局部 */
    ...
}
async weak @Node g(void);               /* ✓ 返回值 */
```

于是 `weak` 形参、局部、返回值在 `async` 里都合法：帧在任务的一生里地址不变，
注册在帧里的弱槽不会悬空。曾经的"帧会被搬到别处"禁令只对帧拷贝后端有意义，
那套后端（c67）现在编译期就拒绝 `async`，禁令与它的检查点
（`c6_async_func_begin()` / `c6_gc_site()` 里的 `!c6_stable_frame`）一并删除。

### 9.3 未完成任务必须回卷（`__c6_co_destroy`）

现状：`__c6_co_destroy()`（`lib/libtcc1.c:1580`）只 `unlink` + 清槽，**不执行 body 的清理**。
加了引用计数之后，一个挂起中没有被 `await` 完的任务会把它的全部引用漏掉。

修法：新增 `C6_CO_KIND_ABORT` 与 `__c6_co_abort(self)`：

```c
void __c6_co_abort(void *self)
{
    TccCoTask *t = self;
    if (t->state == C6_CO_STATE_DONE) return;
    t->kind = C6_CO_KIND_ABORT;
    t->state = C6_CO_STATE_READY;   /* 解除 fd/child 等待 */
    tcc_co_step_once(t);            /* 恢复帧 → 走清理派发器 → finish(ABORT) */
}
void __c6_co_destroy(void *self)
{
    ...
    if (((TccCoTask *)self)->state != C6_CO_STATE_DONE)
        __c6_co_abort(self);
    ... unlink / free ...
}
```

**落地（P7）**：检查点不放在末尾的恢复派发器里，而放在**每个 `await` 的恢复点**
（`c6_async_suspend()` 里 `c6_def_label()` 之后）：

```c
if (__c6_co_kind(task) == C6_CO_KIND_ABORT) {
    /* 跑 errdefer + defer + 内联释放 */
    __c6_co_finish(task, C6_CO_KIND_ABORT);
    return C6_CO_KIND_ABORT;
}
```

原因：末尾派发器是在函数作用域关闭**之后**生成的，那时 `defer_state.enabled`
已经复位，`c6_defer_begin()` 是空操作（第一版就栽在这里：只发出 `finish`，
清理体没进去，帧里的引用照样漏）；而且 `c6_defer_func_end()` 已经把按记录
派发的跳转表 patch 完了，事后补发会悬空。恢复点在 body 中间，defer 机制还活着，
跳转表也还没定稿。代价是每个 `await` 多一次 `__c6_co_kind()` 调用。

一步就能跑完，因为清理体里禁止 `await`（`tcc/tccgen.c:13508` 已有该检查）。

**怎么走到这条路（P7 之后的补充）**：`task t = f(...)` 只创建任务，body 在
`t->wait()` / `t->start()` 之前根本没跑过，所以"回卷一个挂起的任务"要靠
`t->start()`：它把 body 推进到第一个挂起点就返回，句柄还活着；作用域结束时
`__c6_co_destroy()` 看到 `state != DONE` → `__c6_co_abort()` → body 从挂起点
恢复一次，走上面那段清理派发器 → `finish(ABORT)`。`tests/async_frame.c6` 的
"挂起的任务被销毁时回卷"就是这条路径（defer 跑了、托管引用计数回到基线）。

这条路径原来还有一个坑：挂起时会把线程的 **defer 栈指针**和 context 栈指针
交出去，任务如果就挂在那儿不恢复，调用者的 `defer` 与 `task` 清理会看到"空的
context 栈"而被**静默跳过**。现在 `defer` 的状态住在函数帧里（见 8.2），
`__c6_co_suspend()` 只停三个和错误槽有关的字段，这对指针已经不存在了：
回卷只走 body 自己的清理派发器。

**预算（§11.4）**：已经是「每次释放最多 256 个」（`c6_gc_drain(th,
C6_GC_DRAIN_BUDGET)` 在 `c6_gc_drop()` 里，`lib/libtcc1.c:1271`），
级联释放跨多次释放摊平，不需要编译器在 `new`/作用域退出处再插调用；
`__c6_gc_drain_budget(n)` 仍导出给想手工跑完队列的代码（`tests/gc_field.c6`
的长链用例用它）。

### 9.4 所有权的跨帧/跨线程转移

- `spawn f(args)`：实参块由 `__tcc_sp_args(size)` 分配、trampoline 调用后 `free`
  （`lib/libtcc1.c:789`/`800`）。托管实参进入块时 +1，块释放时 −1；
  取消“`spawn` 边界不能带托管引用”的旧限制；
- `await f(args)` / `task t = f(args)`：参数被拷进 `struct NAME_task` 前缀
  （`c6_async_call`，`tcc/tccgen.c` 内），+1 进任务；任务 slot 归还
  （`__c6_co_destroy`/池回收）时 −1；
- 约束：跨帧/跨线程只能传 `shared @T`（`plain @T` 编译错误），载荷本身仍由
  用户同步。

**落地方式（P5）**：

1. `spawn`：实参块的槽类型直接用声明类型（带限定符），`vstore()` 钩子 +1；
   `c6_sp_emit()` 生成的 trampoline 在 `return 0` 之前对每个托管形参发
   `__c6_gc_release((void **)((char *)__c6_arg + ofs), mode)`，−1；
2. `async`：`c6_async_call()` 把任务前缀槽的类型换成声明类型（+1）；
   任务 slot 的归还点不在 body 里（body 可能永远跑不完），而在
   `__c6_co_destroy()`：`struct __c6_desc` 增加第 5 个成员
   `void (*release)(void *)`，编译器为每个 async 函数发射
   `static void __c6_rel_NAME(void *t)`，逐字段
   `__c6_gc_release(&t->pN/val, mode)`；为 0 时不发（任务不带引用）；
3. body 一侧：`c6_async_params_copy()` 用 `c6_gc_no_hook` 把前缀槽拷进帧内
   局部（不 +1、不注册释放点），并给局部打 `C6_GQ_BORROW` —— 形参是借用；
4. 检查点在**声明处**（`c6_async_declare()`），一次报出，覆盖形参与返回值；
   `weak` 由 §9.2 单独报（判定顺序：先 `weak` 再 `shared`）。

---

## 10. 线程与原子计数

**原子与否是对象自己的属性**（`h->meta` 里的 `C6_GC_ATOMIC`），不是槽的：
同一个对象可能同时被 `@T` 局部和 `shared @T` 槽指着，加减必须一致，所以由
`new shared T` 在分配时决定，之后每个加减都按头里的标志走。由此得出
E-GC15：`shared @T p = new T;` 会拿到一个计数非原子的对象，跨线程加减会
丢更新（P5 里实测：两个线程各 −1，计数只掉一次），编译期直接拒掉。

`shared @T` 的计数用 `__atomic_fetch_add` / `__atomic_sub_fetch`（`ACQ_REL`）。
**工具链不需要额外库**：`lib/atomic.S`（GCC libatomic 提取版，覆盖 i386/x86_64/
arm/aarch64/riscv 的 load/store/compare_exchange/fence）与 `lib/stdatomic.c`
（在其上生成全部 `__atomic_fetch_*`）已经在 `libtcc1.a` 里（`tcc/lib/Makefile:43`
的 `COMMON_O`），所以 `-run`、静态链接、C99 后端都能解析。

- 计数之外的全局结构只有两个：弱槽注册表（冷路径，全局自旋锁）与全局根链表
  （登记全局根的一次性初始化用）；引用计数热路径上**没有锁**；
- c6 的翻译单元里 `parse_btype` 的 `__atomic_*` 已经可用（`parse_atomic`，
  `tcc/tccgen.c:6265` 附近，经 helper 调用）；libtcc1.c 里需要自己
  `#define __ATOMIC_ACQ_REL 4`（tcc 不预定义，`lib/stdatomic.c` 也是这么做的）。

---

## 11. 性能

### 11.1 单操作成本（x86_64，热缓存，gcc -O2，跨 TU 不内联）

| 操作 | 成本 | 备注 |
|---|---|---|
| plain incref+decref | 4.44 ns/对（≈2.2 ns/op） | 调用 + 内存读改写 |
| 原子 incref+decref（ACQ_REL） | 16.77 ns/对（≈8.4 ns/op） | 走 helper 调用 + `lock xadd` |
| 单次原子 op（循环内） | 7.1 ns（141.7 Mops/s） | 单线程上限 |
| 全局 mutex op | 23.5 ns（42.6 Mops/s） | 仅作对照，设计不用 |
| 作用域退出（16 个引用，派发型） | 171.6 ns（10.7 ns/引用） | 现有 defer 派发器 |
| 作用域退出（16 个引用，内联） | 39.3 ns（2.5 ns/引用） | 本设计采用 |
| `malloc`+`free` 32B | 14.1 ns | 分配本身 |

对照 C++（同机 gcc -O2，内联原子）：`shared_ptr` 拷贝+释放 6.32 ns、
`make_shared`+析构 21.07 ns、`unique_ptr(new)`+析构 18.00 ns、
`weak_ptr` 创建+lock+释放 18.44 ns；`sizeof` 分别 16/16/8 字节。

结论：单线程 `@T` 比 `shared_ptr` 快（2.2 vs 6.3 ns/op 对），
`shared @T` 与它同量级（8 vs 6 ns，差在 tcc 强制走 helper 调用）。
内存上每对象 32 字节头，比 `shared_ptr` 的 16 字节值 + 单独控制块略省一次分配。

### 11.2 多线程扩展性

| 场景 | 4 线程总吞吐 |
|---|---|
| 原子计数，每线程各自的对象 | 568.5 Mops/s（线性） |
| 原子计数，同一个对象 | 51.3 Mops/s（单缓存行瓶颈） |
| 一把全局锁 | 14.1 Mops/s |

即：per-object 原子最优；同一对象的争用是缓存行物理极限，per-object 锁只会更差；
全局锁最差（比 per-object 原子慢 40 倍）。

### 11.3 为什么不用 per-object 锁

1. 它仍然需要一条原子指令（CAS/`lock xadd`）来实现锁本身 —— 既然有了原子指令，
   计数直接原子就够了，锁是纯附加开销；
2. 多一个头部字（glibc 的 `pthread_mutex_t` 是 40 字节），要 init/destroy；
3. 同一对象上的争用瓶颈与原子计数完全相同（同一条缓存行），没有任何收益；
4. 不同对象上的并发本来就已经被 per-object 原子完美扩展（568 Mops/s 线性）。

### 11.4 级联释放延迟（唯一的“暂停”）

1M 节点链的整链释放：**12.0 ms（12 ns/节点）**，一次性发生在某个 `decref` 里。
预算化（每次 256 个）之后：总时间 14.9 ms，**单次调用最坏 40 µs**。
程序可以在任意安全点主动摊平（`p = NULL` 逐个断开，见 §12 的环断开写法）。

典型程序口径：建 1M 节点链表 ≈ 1M×14.1（malloc） + 1M×~3（store） + 1M×12.1（释放）
≈ 30 ms，对比手写 `malloc`/`free` 的 ~24 ms → 约 1.25 倍；
对象长寿的程序这部分开销趋近 0（成本按次，不按字节/秒）。

---

## 12. 与 C++ 智能指针的关系

| | `unique_ptr` | `shared_ptr` | `weak_ptr` | 本设计 |
|---|---|---|---|---|
| 值大小 | 8 | 16 | 16 | 8（`@T` 就是裸指针） |
| 计数位置 | 无 | 控制块（`make_shared` 合入对象） | 控制块 | 对象头（32B） |
| 计数原子性 | — | 总是原子 | 总是原子 | 可选：`shared` |
| 拷贝 | 禁止 | +1 | +1 | 禁止，只能进托管槽（编译器强制） |
| 析构触发 | 作用域结束 | 最后一个强引用 | — | 最后一个引用 + `drop` 方法 |
| 环 | 不可能 | 泄漏 | — | **编译期硬错误** |
| 自定义删除器 | ✓ | ✓ | — | ✗（V1） |
| 跨 C ABI | ✗ | ✗ | ✗ | ✓（裸指针形状） |
| 显式提前释放 | `reset()` | `reset()` | `reset()` | `p = NULL`（不引入 `reset`） |

`@T` ≈ Rust `Rc<T>`，`shared @T` ≈ `Arc<T>`，`weak @T` ≈ `Weak<T>`，
`drop` 方法 ≈ Rust `Drop`；额外的部分是编译器强制所有权 + 静态环诊断
（Rust 的 `Rc` 环是静默泄漏，C++ 也没有编译期检查）。

---

## 13. 语言的边界：哪些情况检测不到环

能闭合（编译器可以报错）：

1. 强转打洞 —— 溯源位 + 所有存储点检查（`T *p = managed;` 报错）；
2. 保存到非托管槽（`void*` 字段、整型强转、`memcpy`）；
3. 跨函数借出逃逸 —— 形参 `c6_borrow` 标注，未标注时 `-Wgc-borrow`；
4. 跨 TU —— `.c6.gcgraph` 段 + 链接期检查；
5. 不完整类型 —— 由链接期给结论；
6. 自环与多节点环 —— 硬错误（§8）。

闭不上（文档化，不静默）：

1. 手写汇编、外部（非本编译器编译的）代码里构造的指针；
2. `c6_graph` 豁免类型里的运行期环；
3. 主动调用内部 `__c6_gc_*`（不导出给用户，头文件里只给编译器用）。

---

## 14. 诊断清单

| 编号 | 场景 | 级别 | 文案要点 |
|---|---|---|---|
| E-GC1 | 环（≥2 节点或自环） | error | `reference cycle: A -b-> B -a-> A; break it with 'weak' or 'own' (type 'A' at a.c:3)` |
| E-GC2 | `weak` 出现在 async 函数 | error | `'weak' cannot be declared in an async function ...` |
| E-GC3 | 保存借出的托管指针 | error | `storing a borrowed reference keeps it alive without counting` |
| E-GC4 | FRESH 未消费 / 重复消费 | error | `a fresh reference must be stored exactly once` |
| E-GC5 | 含托管字段的结构体按值声明/赋值/传参/返回 | error | `'%s' holds managed references: %s is not possible. Allocate it with 'new' and hold it in '@%s'`（`%s` = `a declared object`/`an assignment`/`a parameter`/`a returned value`） |
| E-GC6 | union 含托管字段 | error | `'%s' is a union with a managed field: which member is alive cannot be known, use a struct` |
| E-GC6b | 含托管字段的结构体在块内定义 | error | `'%s' holds managed references and is defined in a block: define it at file scope` |
| E-GC6c | 柔性数组元素是托管引用 | error | `'%s' has a flexible array of managed references: allocate it with 'new T[n]' instead` |
| E-GC6d | 新引用存进非托管槽 | error | `a new reference cannot be stored in '%s': it is not a managed reference (declare it as '@T')` |
| E-GC7 | `memcpy`/`memmove` 作用于含引用的对象 | error | `... breaks reference counting; use a typed copy or 'own'` |
| E-GC8 | 显式调用析构 `p->drop()` / `drop(p)` | error | `a destructor cannot be called: it runs when the last reference is released (use 'p = NULL')` |
| E-GC9 | 析构里 `err`/`try`/`await`/`spawn` | error | `a destructor cannot fail: ...` / `a destructor cannot suspend: ...` |
| E-GC9b | 析构返回值不是 `void` | error | `'drop' must return void: it runs while the object is being released, where a value has nowhere to go` |
| E-GC9c | 析构声明成 `!error` | error | `'drop' cannot fail ('!error'): it runs while the object is being released, where an error has nowhere to go` |
| E-GC9d | 一个类型两个析构 | error | `'%s' already has a destructor`（同一 TU 内通常先报 `redefinition of 'T_drop'`） |
| E-GC10 | `@T` 跨线程/跨帧 | error | `passing a reference to 'spawn' requires 'shared'` / `passing a reference to 'async' requires 'shared'` |
| E-GC11 | `memset` 含引用对象 | warning | 清零会漏引用 |
| E-GC12 | 借出给未标注裸指针形参 | warning `-Wgc-borrow` | 可 `-Werror` |
| E-GC13 | `own` 值被共享 | error | `an 'own' field takes ownership: assign a fresh value or 'move(...)'` |
| E-GC14 | 给借用的引用赋值 | error | `'%s' is a borrow: it owns nothing, so it cannot be reassigned (copy it into an owned local first)` |
| E-GC15 | `shared @T p = new T` | error | `a 'shared' slot needs an object with an atomic count: create it with 'new shared T', not 'new T'` |
| E-GC16 | `move(x)` 作用在非托管/非槽上 | error | `'move' needs a managed reference held in a slot, not a '%s'` |
| E-GC17 | `move(p)` 之后、重新赋值之前使用 `p`（读/比较/解引用/传参/再 `move`） | error | `'%s' was moved: assign a new reference before using it` |

---

## 15. C99 后端约束

- 所有新符号必须进 `c99_runtime_names[]`（`tcc/c99-gen.c:685`），
  否则 `-c99` 输出不会声明/发射运行时；
- 运行时实现同时写 `lib/libtcc1.c` 与 `tcc/c99-rt.inc`（后者被 `c99-rt_.h`
  生成到 `c99_runtime[]`）；
- 描述符是静态数据（`static __c99_unused unsigned char ...`），析构是普通函数，
  不需要新机制；
- 跨 TU 的链接期环检查在 `-c99` 路径上**没有**（外部链接器不做，而且 c99 后端
  只输出 C 源码、没有段的概念），只做单 TU 检查 —— 文档写明。`-c99` 输出走的是
  `c99_output_file()`，`.c6.gcgraph` 那套数据段在那里根本不会生成，所以检查
  什么也看不到；用例用 `// NATIVE-ONLY <片段>` 标注，`run-tests.sh` 在 c99 后端
  跳过该片段（`tests/errors/gc_cycle_cross_tu.c6` 就是这么标的）。

---

## 16. 实施中已发现的既有缺陷（必须一并处理）

| 编号 | 缺陷 | 位置 | 处理 |
|---|---|---|---|
| H1 | `__c6_co_destroy` 不回卷未完成任务 | `lib/libtcc1.c:1580` | `__c6_co_abort`（§9.3） |
| H2 | async 帧搬动 → 帧内绝对地址失效 | `lib/libtcc1.c` 的 `__c6_co_suspend`/`restore` | 已修：帧住进任务（§9.1，A3，五个后端 + C99）；拷贝模型与 `weak` 禁令已删除，c67 上 `async` 编译期报错 |
| H4 | 方法调用的接收者被拷进临时槽时走了普通托管赋值：临时槽没初始化却先读旧值去释放（栈垃圾 → 崩），而且给接收者白加一个永不释放的引用 | `tcc/tccgen.c` 的 `c6_method_call_prep()` | 已修：接收者那份拷贝是借用，按 `C6_GCM_INIT \| C6_GCM_FRESH` 存（不计数、不读旧值） |
| H3 | defer 栈满 8192 静默丢记录 | 原 `lib/libtcc1.c` 的 defer 记录栈 | 已消灭：记录栈整个删掉，清理状态搬进函数帧（§6、[grammar.md](grammar.md) 8.2），上限变成编译期报错（C101/C102） |

---

## 17. 未决问题

见 [gc-todo.md](gc-todo.md) 的 D1–D6。此外 P6 之后仍开着两件：

1. **链接期跨 TU 环检测只在原生后端**（§8.2 第 2 条、§15）：`-c99` 输出 C
   源码，没有段也没有链接期，跨 TU 环在那边仍然看不到；
2. **`struct Node c6_graph` 豁免**（§8.4 / D2）没做：共享边的通用图目前只能
   写成 `own` 树或 `weak` 边 —— 这是语言取舍，真要做得连运行期环回收一起想。

返回托管引用时“每调用一次就多一个计数”的旧问题（本节原来的第 3 条）已经
不存在：`c6_gc_return_hook()` 在返回点把一份引用交给调用者，`tests/gc.c6` 的
`return 把引用交给调用者` 与 `tests/gc_return.c6`（新建对象 / 借字段 / 形参
原样返回 / 循环多次调用）逐条断言计数会回到基线。
