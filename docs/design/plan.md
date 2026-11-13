# c6 语言扩展实现方案（深度评审修订版）

> **状态**：已落地（全部阶段完成） · **索引**：[文档索引](../README.md) · **语法**：[grammar.md](../reference/grammar.md) · **实现**：[implementation-notes.md](implementation-notes.md) · **状态表**：[status.md](../history/status.md)
>
> 一句话：每个特性**为什么**这么设计、哪些方案被评审否决。语法与用法不在这里，
> 分别见 [grammar.md](../reference/grammar.md) 与 [manual/](../manual/)。
> 原先的第 11 节状态表已抽到 [status.md](../history/status.md)。

> 本文档在上一版基础上，结合 TCC 源码级约束进行深度评审与修复。核心约束重申：**TCC 是单趟编译器，无 AST，解析与代码生成交织，任何扩展必须在递归下降的特定时机就地插入，不得依赖事后重写**。以下先给出本轮深度评审发现的**阻塞性缺口与修复**，再输出完整方案。


## 0. 深度评审：阻塞性缺口与修复摘要

| 编号 | 缺口 | 严重度 | 修复 |
|---|---|---|---|
| **G1** | `defer` 的 `active_slot` 分配在函数 prologue，但条件/循环中 `defer` 可能多次注册，单一槽位无法表达“多次注册、多次执行” | 阻塞 | 改为**运行时 defer 记录链表**（每注册一次生成一条记录，含函数指针+参数），而非编译期单槽；清理时遍历链表逆序调用 |
| **G2** | `defer` 清理代码“生成在函数末尾清理区”与 TCC 的代码生成模型冲突：TCC 顺序追加机器码，不能在中间“预留”末尾区域 | 阻塞 | 采用**就地生成清理块 + 跳转绕过**模式：注册时立即生成清理块和跳过跳转；清理序列通过跳转链调用，不依赖“末尾集中区域” |
| **G3** | `goto` 跨 defer 作用域的清理判定：原方案说“复用 `local_label_stack`”，但 TCC 的 `local_label_stack` 仅记录标签，不记录作用域深度 | 高 | 引入独立的**`ScopeTracker` 栈**，与块解析同步压入/弹出，记录每层的 defer 记录起始位置；`goto` 时比对源/目标作用域，生成对应清理 |
| **G4** | `interface` 的 vtable 生成时机与单趟编译冲突：转换点可能早于实现声明 | 高 | 方法先于转换点可见即可；首次转换时登记 (类型, 接口) 并建 vtable 前置符号，**TU 末尾统一生成** `static` vtable 对象，调用点只查表不做代码回填 |
| **G5** | `err()` 的 `goto __err_exit` 假设存在统一出口，但 TCC 没有“函数末尾统一清理标签”的现成机制 | 高 | 采用**每错误返回点就地生成清理序列**：`err()` 处直接展开 errdefer/defer 的逆序调用，然后 `return` 哨兵；不依赖间接跳转 |
| **G6** | `test` 块的 runner 在 `-run` 模式下由 `libtcc.c` 遍历 `TCCState->tests`，但 TCC 的 `-run` 入口是用户 `main`，没有“先跑测试再跑 main”的钩子 | 中 | `-test` 模式下**不调用用户 `main`**，`libtcc.c` 直接遍历测试表；`-test` 与正常 `-run` 互斥，文档明确 |
| **G7** | 扩展开关通过“运行时关键字映射”实现，但 TCC 的 `tok_alloc` 在词法阶段就将字符串 intern 为 `TokenSym`，后期切换映射不可行 | 中 | 改为**编译期条件编译**：`tcctok.h` 中 `#ifdef C6_EXT` 注册新 token；扩展关闭时这些字符串根本不进入关键字映射，零运行时开销 |
| **G8** | 宏体里局部变量的卫生重命名通过 `TokenSym` 插入唯一名，但 TCC 的 `TokenSym` 是全局 intern 表，宏展开产生的临时标识符会永久累积 | 中 | 重命名使用**展开计数器后缀**（`<宏名>__<变量名>__h<展开号>`，如 `swap__tmp__h17`），但不在 `TokenSym` 中持久化；展开结束后从 intern 表移除，或接受可控的内存增长（首版可接受，后续优化） |
| **G9** | 命名空间要在 `tccgen.c` 顶层处理，但 TCC 的顶层声明循环 (`decl`) 不天然支持“命名块”语法 | 中 | 命名空间 = 文件：`module NAME;` 给整个文件的文件作用域符号加前缀，路径就是名字（见第 6 章）。不需要“顶层块作用域”，也不做嵌套 mangle |
| **G10** | `err` 的错误槽“通过隐藏参数传递”在 TCC 的调用约定中无现成支持 | 中 | 使用**每线程错误槽**（`pthread_key` + `pthread_getspecific`，不是 `__thread`：`-run` 不支持 TLS），调用者从槽读取；`spawn` 出去的线程有自己的槽，跨线程传播需手动复制。隐藏参数作为后续优化 |


## 1. 总体架构与扩展开关

### 1.1 挂载点

| 源文件 | 职责 | 扩展挂载点 |
|---|---|---|
| `tcctok.h` | 关键字与 token 编号 | `#ifdef C6_EXT` 注册新 token；关闭时零映射 |
| `tccpp.c` | 预处理与词法分析 | 宏体的捕获 / 角色检查 / 卫生重命名（`__local` 与自动扫描）、include 路径扩展 |
| `tccgen.c` | 语法分析与代码生成 | `module/import` mangle 与 `mod.member`、结构体方法注册、`interface` 转换与 vtable 生成、`defer/errdefer/err` 就地代码生成、`test` 块注册 |
| `tcc.h` / `libtcc.c` | 符号表与编译状态 | `TCCState` 扩展字段、`-test` runner、扩展开关 |

### 1.2 扩展开关（修复 G7）

```c
/* tcctok.h */
#ifdef C6_EXT
DEF(TOK_C6_PACKAGE, "package")   /* 只剩迁移提示 */
DEF(TOK_IMPORT,    "import")
DEF(TOK_INTERFACE, "interface")
DEF(TOK_DEFER,     "defer")
DEF(TOK_ERRDEFER,  "errdefer")
DEF(TOK_ERR,       "err")
DEF(TOK_TEST,      "test")
#endif
```

- 开关：**只有源文件后缀**。`.c6` 文件对自身翻译单元开启扩展，`.c` 文件永远
  不开启；`-std=c6` 已删除（传了直接报错），见 `tcc/libtcc.c` 的
  `guess_filetype()` / `tcc_add_file_internal()`。
- 关闭时：`C6_EXT` 不定义，这些字符串保持普通标识符，**零运行时映射开销**。
- 开启时：`C6_EXT` 定义，token 进入 lexer 关键字映射。
- `__C6__` 宏同步定义，供源码条件编译。


## 2. `defer`：就地生成 + 运行时记录（修复 G1–G3）

### 2.1 语义

`defer S;` 在**执行到该语句时注册**，块退出时按**后进先出**执行。条件分支中的 `defer` 只在对应路径注册后生效；循环中的 `defer` 每次迭代可注册一次，块退出时执行全部已注册记录。

### 2.2 代码生成模型（修复 G2）

TCC 顺序追加机器码，不能“预留末尾区域”。因此采用**就地生成 + 跳转绕过**：

```
<当前执行流>
  ... 注册 defer 的代码：将记录追加到运行时链表 ...
  jmp L_after_cleanup_N
L_cleanup_N:
  <S 的代码>
  jmp L_after_cleanup_N_end   ; 或直接 ret 到调用方
L_after_cleanup_N_end:
  ...
```

注册时：

1. 生成记录结构 `{ fn, arg }` 的初始化代码，追加到当前作用域的运行时 defer 链表；
2. 生成 `jmp L_after_cleanup_N` 跳过清理块；
3. 生成清理块 `L_cleanup_N: <S> ; ret`（清理块以 `ret` 或跳回清理调度器结束）。

清理时（块自然退出、`return`、`break`、`continue`、`goto`）：

- 遍历运行时链表的逆序，对每个记录生成 `call fn(arg)`；
- 不依赖“函数末尾统一清理标签”，每个出口就地展开。

### 2.3 运行时记录

```c
typedef struct DeferRecord {
    void (*fn)(void *);
    void *arg;
    struct DeferRecord *prev;
} DeferRecord;
```

每个函数帧在 prologue 分配 `DeferRecord *defer_top = NULL;`。`defer S;` 注册时，生成代码分配一条记录（栈上或固定池），`fn` 为 `S` 包装的 thunk，`arg` 为所需上下文。

### 2.4 `ScopeTracker` 与 `goto`（修复 G3）

引入独立栈：

```c
typedef struct ScopeTracker {
    struct ScopeTracker *prev;
    DeferRecord *defer_start;   /* 进入本块时的 defer_top 快照 */
    int depth;
} ScopeTracker;
```

- `block()` 进入：压入 `ScopeTracker`，记录 `defer_start = defer_top`；
- `block()` 退出：弹栈，生成“从 `defer_top` 逆序到 `defer_start`”的清理序列；
- `goto`：比对源作用域和目标作用域的 `ScopeTracker` 链，生成从源到目标的逆序清理；
- `return`：生成从当前 `defer_top` 到函数根 `defer_start` 的完整逆序清理，返回值先存入临时量。

> slimcc 的经验表明，**VLA de-allocation 的 `ScopeTracker` 结构可以直接复用于 `defer`**。TCC 已有 VLA 栈跟踪机制，可借鉴而非从零构建。

### 2.5 `return expr` 的返回值保护

```c
ret_tmp = expr;          // 先求值并存入临时量
<逆序清理序列>
return ret_tmp;
```

清理函数可能 clobber 寄存器，因此**返回值必须先落栈临时量**。slimcc 明确指出“dealing with return values potentially clobbered by cleanup functions”是主要工作量之一。


## 3. `errdefer` / `err`：逐层传播 + 就地清理（修复 G5、G10）

### 3.1 主模型

**每帧错误槽 + 返回码/哨兵逐层传播。** 不用 `longjmp` 跨帧，因为 `longjmp` 会跳过中间帧的清理。

### 3.2 运行时结构

```c
/* 每线程状态：pthread_key -> TccThCtx（-run 不支持 TLS，因此不能用 __thread） */
struct TccThCtx {
    TccDefer  *defer;      /* defer 记录栈 */
    int        err_flag;   /* 错误槽 */
    int        err_id;
    const char *err_msg;
    TccCoPool *co;         /* 本线程的协程池 */
    TccThRec  *th_list;    /* 本线程 spawn 出去的线程 */
};
```

`tcc_th()` 用 `pthread_getspecific` 取当前线程的 `TccThCtx`，第一次用时
`pthread_key_create` 建键并分配；`spawn` 出去的线程继承不到启动方的上下文，
第一次调用 `tcc_th()` 时拿到自己的那一份。

每个函数帧 prologue **不需要**独立错误槽；错误槽是每线程的，但函数在**错误返回点**显式读取并决定传播。递归场景下，每层在调用子函数后检查 `__tcc_err.flag`，需要时复制到本地变量后清除，避免串扰。

### 3.3 `err(id, msg)` 生成（修复 G5）

`err(id, msg)` 处**就地展开清理序列**，不跳转间接出口：

```c
__tcc_err.id = id;
__tcc_err.msg = msg;
__tcc_err.flag = 1;
/* 就地逆序执行当前作用域到函数根的 errdefer（条件 __tcc_err.flag）和 defer */
<清理序列>
return ERR_SENTINEL;   /* 哨兵值 */
```

`errdefer` 的清理调用条件：`if (__tcc_err.flag) { call fn(arg); }`。

### 3.4 调用者传播（`try` / `catch` / `T!error`）

被调函数在返回类型上标注 `!error`，调用方用 `try` 原样传播、用 `catch`
就地处理：

```c
static int!error may_fail(int n) {
    if (n > 0) err(n, "nope");
    return n * 10;
}

static int!error propagate(int n) {
    int v = (try may_fail(n));       /* 失败时执行本帧清理并返回哨兵 */
    return v + 1;
}

int handle(int n) {
    int v = may_fail(n) catch e {    /* e.id / e.msg 即 err() 传入的值 */
        printf("caught %d: %s\n", e.id, e.msg);
        return -1;                   /* 处理块必须自己离开 */
    };
    return v;
}
```

实现要点（单趟、无 AST 的约束下）：

- `!error` 只是函数类型上的一个属性位（`FuncAttr.func_fallible`），不改变调用约定；
  它在 `decl()` 里由 `c6_parse_fallible_suffix()` 解析，前置/后缀两种位置等价，
  原型上的标记会传递到之后的定义；
- `try` 挂在 `unary()` 的前缀分支（`c6_try_parse`）：就地调用、检查错误槽、
  失败时走 `c6_emit_propagate()`（与 `err()` 共用同一条清理+哨兵返回路径）；
- `catch` 挂在 `expr_cond()` 之下的独立一层 `expr_catch()`（`c6_catch_parse`）：
  先求出调用值并 `save_regs(0)` 落到栈上，再测错误槽；失败分支用
  `__tcc_error_take(&e)` 快照并**清空**错误槽，然后编译处理块；
- 判断“操作数确实是调用”用的是代码生成期的标志 `c6_value_fallible`：
  函数调用处置位，任何把它组合掉的操作（`gen_op` / `gen_cast` / `vstore` /
  `indir` / `gaddrof` / `inc` / 后缀运算）都会清零；
- `catch` 处理块用 `c6_last_stmt_leaves` 检查是否以 `return` / `err(id, msg)` /
  `goto` / `break` / `continue` 离开，否则编译报错，避免静默落回成功路径；
- `test` 块合成的函数也带 `func_fallible`，因此 test 块里可以直接 `try`；
- `--run` 下错误槽接口是普通全局变量（无 TLS），与 `err` 的限制一致。

### 3.5 规则

- 普通 `return` **不清除** `__tcc_err.flag`，由调用者负责；文档明确此约定；
- `msg` 生命周期：首版要求静态字符串或调用者管理；
- 递归中每层调用子函数后应立即检查并快照 `__tcc_err`，再清除，避免串扰；
- `catch` 进入处理块前**快照并清空**错误槽（`__tcc_error_take`）；此外每次
  `T!error` 调用之前都会插入 `__tcc_err_clear()`，所以槽里只可能反映本次调用
  的错误，连续两个 `catch` 也不会把上一次的错误误判成这一次的。


## 4. `interface`：结构化实现 + 胖指针 ABI（修复 G4）

### 4.1 设计

**函数体只依赖固定 ABI（胖指针 + vtable），调用点构造接口值。** 这样函数体代码生成与具体类型解耦。

实现关系不显式声明：**结构体拥有接口要求的全部方法，就实现了该接口**。方法本身
也是普通函数——文件作用域里首参是指向该结构体指针的函数（见 6.4），因此
`impl Drawable for Circle { void draw(Circle *self) ... }` 里唯一有价值的信息
（方法集合）已经由方法表提供，`impl` 语法被删除。

### 4.2 语法

```c
interface Drawable {
    void draw();
    int  area();
}

struct Circle { int r; };

/* 这两个函数就是实现：调用约定不变，符号是 Circle_draw / Circle_area
   （源码名额外发一个弱别名，见 6.4） */
void draw(Circle *self) { /* ... */ }
int  area(Circle *self) { return 3 * self->r * self->r; }

void render(Drawable d) {
    d.draw();               // 生成 d.vt->draw(d.self)
}

void use(void) {
    Circle c = {0};
    render(c);              // 调用点构造 {&c, vt_Circle_Drawable}
}
```

### 4.3 运行时表示

```c
struct Drawable {
    void *self;
    const struct DrawableVTable *vt;
};
struct DrawableVTable {
    void (*draw)(void *self);
    int  (*area)(void *self);
};
```

方法就是首参为 `void *` 的普通函数，因此 vtable 直接存放方法地址，**不需要 thunk**
（`void *self` 与 `struct Circle *self` 的表示相同）：

```c
static const struct Drawable_vt __tcc_vt_Circle_Drawable = {
    (void (*)(void))draw, (void (*)(void))area
};
```

符号命名：vtable 对象 `__tcc_vt_<Type>_<Interface>`（每个 TU 一份 `static`）。

### 4.4 可见性规则（修复 G4）

- 方法**必须在转换点之前**声明或定义（单趟编译约束）；
- 第一次发生 `render(c)` / `Drawable d = c` / `(Drawable)c` 时校验并登记
  `C6AutoImpl{Circle, Drawable, vt_sym}` 到 `TCCState`，同时创建 vtable 的
  前置符号（`extern`，还不是定义）；
- 每个 TU 末尾由 `c6_emit_auto_impls()` 生成
  `static const struct Drawable_vt __tcc_vt_Circle_Drawable = { ... };`；
  生成与 `gen_inline_functions()` 交替进行（`static inline` 函数体在
  TU 末尾才编译，其中可能又发生转换）；每个 TU 一份 `static`，因此多个
  TU 转换同一类型不会符号冲突；
- 跨 TU：头文件里放 `interface` 与方法声明，vtable 由各 TU 按需自行生成。

### 4.5 编译期检查（惰性）

第一次转换时执行：接口存在、类型的方法集合覆盖接口、去掉 self 后签名（返回类型、
参数个数、参数类型）兼容；缺方法或签名不符时诊断为
`'Circle' does not implement interface 'Drawable': missing method 'draw'` 等。
没有“多余方法”检查：类型可以同时实现任意多个接口。


## 5. `test "desc" {}`：内建测试框架（修复 G6）

### 5.1 语法与注册

```c
test "add works" {
    if (add(1, 2) != 3) err(E_TEST, "1+2 != 3");
}
```

顶层构造，描述为字符串字面量。解析时生成 `static int __tcc_test_N(void)` 函数，注册到：

```c
typedef struct TccTestEntry {
    const char *desc;
    int (*fn)(void);
} TccTestEntry;
/* TCCState 扩展 */
int test_count;
TccTestEntry *tests;
```

### 5.2 `-test` 模式（修复 G6）

`-test` 与正常 `-run` **互斥**。`-test` 下 `libtcc.c` 不调用用户 `main`，直接遍历 `TCCState->tests`：

```c
for (int i = 0; i < s->test_count; i++) {
    int r = s->tests[i].fn();
    if (r) fprintf(stderr, "FAIL: %s: %s\n", s->tests[i].desc, __tcc_err.msg);
    else   printf("PASS: %s\n", s->tests[i].desc);
}
```

退出码：全 pass 为 0，否则非 0。`--test-filter=<substr>` 按描述过滤。

### 5.3 与 `err` / `defer` 的交互

test 函数内 `err(id, msg)` 就地展开清理后返回哨兵 `1`；`__tcc_err` 携带 id/msg 供 runner 报告。`defer` 正常执行。`return;` 视为 pass；`return expr;` 首版禁止。

### 5.4 生成独立 runner（`-o` 模式）

仅当用户未定义 `main` 或显式 `--no-main` 时，注入 `__tcc_test_runner` 作为 `main`。


## 6. 命名空间：一个文件一个模块，模块名就是它的路径（修复 G9）

> 历史：本节最初实现的是 `package`（"mangle 语法糖"）。`package` 与后来加的
> `module` 是同一件事的两套写法，两套前缀规则会各自漂移，已经删除（只剩一条
> 迁移诊断）。下面描述统一后的唯一模型。

### 6.1 语法

```c
/* geo/shape.c6 */
module geo.shape;

struct Point { int x, y; };
typedef struct Point Point;
int counter = 0;
int add(int a, int b) { return a + b; }
int area(Point *p) { return p->x * p->y; }     /* geo__shape__Point_area */
```

```c
/* app.c6 */
import geo.shape;

int main(void)
{
    Point p = { 3, 4 };
    geo.shape.counter = 1;                     /* 限定访问 */
    return geo.shape.add(1, 2) + add(1, 2) + area(&p);
}
```

模块名可以分级，分级就是目录：`module geo.shape;` 的文件必须在 `geo/shape.c6`。
一个文件一个模块，块形式、嵌套形式都不再有。

### 6.2 实现（修复 G9）

1. `module NAME;` 是文件的第一条声明（软关键字：文件里已有同名普通标识符时
   按 C 语义处理），设置 `BufferedFile.c6_module = "NAME__"`；
2. 文件作用域的声明经 `c6_mod_name` 加前缀：普通名字 `NAME__sym`，方法
   `<NAME__Tag>_<meth>`；块作用域、`static`、`test`、`__` 开头和 `#include`
   的普通 C 文件不改名；模块文件不发方法的原名弱别名，所以 C 侧只看到前缀名；
3. **路径分级**：`import a.b;` 只按 `a/b.c6`（或 `a/b.c`）查找，顺序是当前
   文件目录 → `-I` → 当前目录。`c6_module_open` 分两趟走完所有根：先找分级
   路径（`c6_module_graded`），全都没有才用扁平拼写（`a.b.c6` / `a__b.c6`）
   报错（`c6_module_flat`），所以正确摆放的模块不会被旁边的散落文件干扰。
   找到的文件必须逐个声明同名模块（`c6_expected_module`），否则
   `module file declares 'a__zzz', expected 'a__b'`；
4. `import NAME;` 打开 `NAME.member` 写法，并用 `decl(VT_CONST)` 重放声明：
   函数只登记原型（`skip_or_save_block`），全局变量只登记 `extern`
   （`c6_skip_decl_tail`），`static` 对象和 `test` 块跳过；找不到文件是错误
   （`cannot find module 'a.b': expected a file at 'a/b.c6'`），循环 import
   报 `circular import of module 'a.b'`；
5. `mod.member`（可多级）由 `unary()` 解析：先按普通 C 标识符查找，命中可见
   对象就保持 C 的成员访问语义；未命中且后随 `.` 时逐段增长前缀，用
   `sym_find_raw(mod__seg)` 查找，命中且该模块已 import（或就是当前模块）才
   接受，剩下的 `.field` 交回普通 postfix 路径；未 import 报
   `'mod' is neither an enum nor an imported module (use 'import mod;')`；
6. 类型位置也接受 `mod.Type`：`parse_btype` 先做一次可回退的扫描
   （`c6_qualified_type`，先 `sym_find` typedef 再 `struct_find` tag），解析到
   `mod__Type`；不是类型时把 token 原样还回，所以 `mod.func(...)` 这类表达式
   语句仍走普通表达式解析。

首版**不导出宏**；`static` 保持文件内可见。

### 6.3 跨文件

没有包清单，也不需要清单：模块名就是路径，`import` 按路径读声明。跨 TU 的符号
仍然是普通的、带前缀的 C 符号。老 C 代码想调用模块导出，手写
`int geo__shape__add(int, int);` 即可；反过来扩展 TU 也能直接调用普通 C 函数。

### 6.4 结构体方法

`ret f(struct T *self, ...)` 在文件作用域注册为 `T` 的方法，符号名是
`<T>_f`；`p->f(a)` 被改写成 `T_f(p, a)`（接收者先存临时槽，返回结构体时排在
sret 缓冲之后）。源码里写的 `f` 另外输出成一个**弱别名**，所以普通调用
`f(p, a)`、手写 C 和纯 C 翻译单元照旧能链接，而不同结构体可以用同名方法而
不必放进不同模块：

```c
struct Circle { int r; };
struct Rect { int w, h; };
int area(struct Circle *c) { return 3 * c->r * c->r; }   /* Circle_area */
int area(struct Rect *r)   { return r->w * r->h; }       /* Rect_area */
```

弱别名只指向本 TU 里第一个取到该名字的方法，真有歧义时链接器挑一个（手写的
强定义优先），所以同时用到两个同名方法时应写 `p->f()`。

别名只在普通名确实属于这个方法时才发。普通名已经被别的函数占着（同名的普通
函数定义，或 libc 头里的声明如 `getpid`）时只输出 `<T>_f`：`p->getpid()` 照旧
可用，`getpid()` 仍然是原来的函数——`c6_method_alias` 用
`is_compatible_types()` 比对两份函数类型，避免把 `getpid()` 的调用点改指到
方法上。

不改名的情形：`static` / `inline` 函数（函数体在 TU 末尾或本文件内生成，
挂不上别名）、`__` 开头的名字（编译器和 libtcc1 自己用的）、系统头文件里的
声明（`p->fflush()`、`p->gettimeofday()` 必须仍然调 libc），以及匿名结构体的
方法（匿名 tag 的编号是每个 TU 各一份）。模块里的方法不需要保留原名：符号只按
接收者取名，模块前缀由 `NAME__` 那一层加在最外面（`geo__shape__Point_area`）。

字段优先，方法须先于调用点声明，`duplicate method` 检查按 (类型, 源码名) 做
（一个类型仍然只能有一个同名方法；同一个符号的第二次定义由 C 的重定义检查报出，
消息是 `redefinition of 'Vec_area'`）。

### 6.5 导出：`-emit-header`

`-emit-header FILE`（配合 `-c`）在模块 TU 末尾把记录下来的导出（`C6HeaderDecl`
数组）写成普通 C 头文件：结构体/枚举/typedef/函数/全局变量，切片和元组生成
`NAME__slice_N` / `NAME__tuple_N` typedef，托管字段另外写出 `__c6_gc_field` /
`__c6_gc_desc` 描述符。C 侧包含头文件即可调用，不需要知道模块的存在。

## 7. 宏：像函数一样写（修复 G8）

### 7.1 语法

宏写在**文件作用域**，没有 `#` 前缀，读起来就是一个函数：开头的角色词说清
展开结果怎么用，每个形参前面也带角色（都不能省）。

```
macro-def = role , identifier , "(" , [ param { "," , param } ] , ")" ,
            "{" , { preprocessing-token } , "}" ;
param     = role , identifier ;
role      = "expr" | "stmt" | "ident" | "type" ;
```

```c
stmt swap(expr a, expr b) {
    typeof(a) tmp = (a);
    (a) = (b);
    (b) = tmp;
}
```

| 角色 | 展开成 | 拿来干什么 |
|---|---|---|
| `expr` | `( 宏体 )` | 当值用；宏体是一个表达式（结尾可带一个 `;`），实参也自动加括号 |
| `stmt` | `{ 宏体 }` | 当一条语句用，可以是任意多条语句 |
| `ident` | 原样 token | `##` 拼名、`#` 字符串化、声明里的名字 |
| `type` | 原样 token | 当类型说明符用 |

角色词是**软关键字**：文件作用域、后面紧跟一个标识符、并且当前没有同名可见
符号时才开启一个宏定义（`tccgen.c` 的文件作用域分派，与 `module` / `spawn`
同一条路子）；别的位置它们是普通标识符。

### 7.2 展开（修复 G8）

1. `macro_subst_tok()` 取到实参后，先对**宏体**做卫生重命名（在参数替换之前，
   所以卫生不会漏进调用者的 token）；
2. 重命名表由两处合成：宏体里 `__local` 标注的名字，加上 `c6_scan_locals()`
   按声明形状（`typeof`/基本类型/typedef 名/`struct TAG`、`for` 头、多声明符、
   `struct`/`union`/`enum` 的 tag）扫出来的局部变量名；
3. 每个名字改写成 `<宏名>__<变量名>__h<N>`，`N` 是展开计数器——带宏名前缀，
   所以不同宏的同名局部变量、同一个宏的多次展开都不会撞；
4. `#` / `##` 的操作数在**定义期**检查：角色不是 `ident` 就报错；
5. `expr` 的宏体在替换后套一层 `( … )`（丢掉它自己结尾的那个 `;`），`stmt`
   套 `{ … }`，`ident` / `type` 原样重放；
6. 递归展开沿用现有机制，每层独立计数。

### 7.3 限制

- `expr` 的体必须是一个表达式。多语句的值宏要写成 `stmt` + 调用点的语句表达式
  `({ … })`——注意这条路径现在有**已知缺陷**（见下），所以 `expr` 只做单表达式。
- **已知缺陷（与本次重构无关，先记在这里）**：`.c6` 里的语句表达式
  `({ … })` 在不少形状下算不出正确的值，纯 C（`.c`）里同样的代码是对的。
  最小复现：`int a = 1, b = 2; int w = ({ a + b; });` 得到 `1` 而不是 `3`；
  带 `?:`、或在赋值右侧用（`w = ({ … });`）也一样。原因在 c6 的清理帧 /
  寄存器分配与 `block(STMT_EXPR)` 的 `vpop(); gexpr();` 交互上，需要单独修。
  这也是 `expr` 角色只接受单表达式、并且展开成 `( … )` 而不是 `({ … })` 的
  直接原因。
- 不支持宏体里引入 `#define`；诊断的行号映射仍是老样子。

## 8. 分阶段实施计划

| 阶段 | 内容 | 关键验收 | 依赖 |
|---|---|---|---|
| **0** | 语义规范、`C6_EXT` 条件编译、测试矩阵 | 扩展关闭零行为变化 | — |
| **1** | `defer`：就地生成清理块 + 运行时记录链表 + `ScopeTracker` | 条件/循环/递归/多 defer 逆序正确；`goto` 跨作用域清理；返回值落栈保护 | 0 |
| **2** | `errdefer + err`：线程局部错误槽 + 就地清理 + 哨兵传播 | 多层传播逐层执行 errdefer；递归不串错误 | 1 |
| **3** | **`test "desc" {}`**：顶层注册、`-test` 模式、`--test-filter`、与 `err` 集成 | PASS/FAIL 正确；退出码正确；后续特性用 `test` 验收 | 2 |
| **4** | 命名空间单文件：`module` mangle、import 按路径加载声明 | 符号可见性/冲突正确；不导出宏 | 3 |
| **5** | `interface`：胖指针 ABI、结构化实现、vtable 生成 | 方法先于转换点；签名检查；用 `test` 验收 | 3 |
| **6** | 跨文件包 + 跨 TU interface：包清单、`extern` vtable 声明 | 跨 TU 链接正确 | 4, 5 |
| **7** | 宏：`expr`/`stmt`/`ident`/`type` 角色声明、自动卫生重命名（`__local` 保留为手动标注） | 正反例通过；用 `test` 验收 | 3 |

**里程碑**：阶段 1 产出可工作的 `defer`；阶段 2 错误传播闭环；阶段 3 测试框架就位；阶段 5 `interface` 可运行；阶段 6 跨 TU 打通；阶段 7 卫生宏完成。


## 9. 测试矩阵与工程要求

### 9.1 测试矩阵

| 特性 | 正向 | 反向 | 边界 |
|---|---|---|---|
| `defer` | 条件/循环/递归/多 defer 逆序 | `defer` 内控制流报错 | `goto` 跨作用域、返回值 clobber |
| `errdefer/err` | 单层/多层传播、递归 | 非错误路径不执行 | 哨兵冲突、错误槽串扰 |
| `test` | PASS/FAIL 报告、过滤 | 空 test 报 PASS、`err` 报 FAIL | 与用户 `main` 共存 |
| `interface` | 结构体自动实现、多接口、调用/赋值/强转 | 缺方法/签名不符/方法晚于转换点 | 同一类型跨 TU、每 TU 一份 vtable |
| 包管理 | 单文件导入、`pkg.member`、多级包 | 未导入使用、未知成员 | 跨 TU 链接、与老 C 代码互操作 |
| 结构体方法 | `p->f()`、方法调方法、sret、`T!error` 方法、不同类型的同名方法（不靠包）、系统头里的 libc 方法 | 不带调用、重复方法、只有 `->` | 纯 C 定义的方法被 `->` 调用、普通调用名有歧义时的弱别名 |
| 卫生宏 | `__local` 重命名正确 | `#`/`##` 用于 `__local` 报错 | 递归展开 |
| 变量初始化 | 各类定义带初始化器（标量/数组/结构体/`for` 声明） | 自动变量、全局缺初始化器报错 | `static` 变量与 VLA 豁免、`extern` 声明/形参/成员/`typedef` 不受限、模块内报原名 |
| 裸 tag 类型名 | 声明/形参/返回/成员/强转/`sizeof`/切片元素/方法接收者/typedef | tag 被普通符号（函数/变量/`stat`）遮蔽、tag 未声明、tag 出作用域、局部 tag 的切片、不完整类型 | 语句开头 `TAG:` 标签、`TAG(...)`、`const`/`typedef` 组合、`typeof`/`_Generic`/`_Alignof`、接口方法签名、跨 TU |
| 函数字面量 | 显式声明/就地调用/推断声明/作实参/结构体成员/数组初值/文件作用域与 `static` 初始化器/变参/`!error`+`catch`/`defer` 体/嵌套字面量/`static inline` 体里/`qsort`/C 回调（`abi/`）/C99 后端；V2（捕获式闭包）：`closure RET (P…)` 类型、隐式按值捕获、`@` 托管引用、函数指针 → 闭包的跳板、`-emit-header`、跨 TU、C99 后端 | 捕获值装进普通函数指针（C93）、捕获 `own`/`weak`/数组/VLA/块作用域 `static`/`drop` 结构体等（C94）、`shared`/`weak`/`own` 闭包与跨线程任务（C96）、不透明操作（C97）、自捕获（C98）；无名字、带存储类、`async`、缺函数体（C80–C83） | 每处字面量各是一个函数、同一字面量赋给两个变量、和纯 C 函数指针互相赋值、`-emit-header` 不导出、`module` 不改名 |
| 元组 `(T1, T2)` | 返回值/解构声明/解构赋值/`_`/`t[k]`/参数/成员/数组元素/嵌套/复合字面量/`sizeof`/`async`+`await`/`spawn`/`!error`/跨 TU/与 struct 的 ABI 互通/单条语句体与语句表达式里的解构 | 元素是 `void`/函数/VLA/数组/不完整/局部 tag/`error`、下标非常量或越界、解构个数不符、混写类型与名字、全 `_`、右边不是元组、非左值目标、`return {…}` 用在标量函数、`float!error` | `#pragma pack` 不影响布局、`(a, b)` 逗号表达式与各种强转/复合字面量不被误判、`_` 别处仍是普通标识符、文件作用域的元组对象 |

### 9.2 工程要求

- 现有 TCC 测试套件在 `C6_EXT` 开/关两种模式下全回归；
- 调试信息保留原始源位置（尤其 `defer` 清理块和卫生宏）；
- `TCCState` 扩展字段保持 ABI 兼容；
- `__tcc_err` 是每线程槽（`pthread_key`，不是 `__thread`）：`spawn` 出去的线程有自己的错误槽，跨线程传播要手动复制；
- 实现前核实 `macro_subst`、`local_label_stack`、`block_cleanup` 等 TCC 内部函数的真实签名与行为；`test` 生成符号加随机前缀避免冲突。


## 10. 最终评审结论

**通过，可进入实施。** 本版修复了上一版中 10 项阻塞/高风险缺口：

- `defer` 从“静态 active 槽”改为**运行时记录链表 + 就地生成清理块 + `ScopeTracker`**，解决了条件/循环/多次注册和 `goto` 跨作用域问题，与 TCC 顺序代码生成模型一致，并借鉴了 slimcc 已验证的 VLA 跟踪复用路径；
- `err(id, msg)` 放弃“统一出口跳转”，改为**就地展开清理序列 + 哨兵返回**，消除了对不存在机制的假设；
- `interface` 的可见性规则只要求**方法先于转换点**，vtable 在 TU 末尾统一生成并保持
  `static`，符合单趟编译与多 TU 约束；
- 扩展开关改为**编译期条件编译**，零运行时开销；
- `test` 的 `-test` 模式与 `-run` 互斥，runner 直接遍历测试表，不依赖用户 `main` 钩子。

**建议按阶段 0 → 1 → 2 → 3 → 4/5 → 6 → 7 实施。** 阶段 1 为第一个可交付里程碑；阶段 3 的 `test` 框架应在后续特性开发前就位，确保每项扩展都有可重复的验收手段。

## 11. 实施状态（全部阶段已完成）

分阶段的状态表（每个阶段的实现函数、落点文件、验收用例）已抽到
[status.md](../history/status.md)。设计动机与评审结论留在本篇；变更史见
[CHANGELOG.md](../../CHANGELOG.md)。


## 裸机验证（fstest-bare）

freestanding 那一套（`libtcc1` 的外部依赖全是 weak，缺 `calloc`/`mem*`/
`pthread_*`/`select` 时运行时自己顶上）到「没有内核」这一步只差一个引导器。
`examples/boot/` 里就是：512 字节 BIOS 引导扇区（`boot.S`，实模式 INT 13h
读盘 → A20/GDT → 32 位保护模式 → 0x90000 建页表、开 PAE/LME → 长模式 →
把 payload 拷到 0x400000、清 `.bss` → `call _start`）、`flat.py`（按 `PT_LOAD`
摊平 payload；`objcopy -O binary` 以最低**节**地址为基准，会让镜像整体错位
0x498 字节）、`main.c6`（c6 程序本体，输出就是两条 `outb`）。

`cmake --build build --target fstest-bare` 在 `qemu-system-x86_64` 上空机器启动（有 `/dev/kvm` 就
KVM，否则 TCG），断言全过时 payload 往 0x501 写 0，qemu 退出码 1；失败时
打印 `bare metal: FAILED`、退出码非 1。踩过的坑：长模式下不开 `CR4.OSFXSR`
的话 payload 第一条 `movq %xmm0` 就 `#UD`；引导扇区必须正好 512 字节且以
`0x55AA` 结尾；读盘每块 ≤ 16 扇区以免撞 64KB 段边界。tcc 的汇编器不认
`.code16`/`.code32`，所以只有引导扇区这几十行走 binutils 的 `as`。

协程（`co` / `async` / `await` / `task`）同样能在裸机上跑：运行时状态部在
`libtcc1` 的协程池里，调度器 `tcc_co_pump()` 只在有任务停在 fd 上时才用到
`select`；`await c6_io(...)` 是唯一需要外部实现的入口，缺 `select` 时
`__c6_co_run()` 返回 -1（不会链接失败）。`examples/boot/main.c6` 的裸机用例
覆盖了交错的两个顶层 `co`、循环里的 `await`、失败任务计数和 `c6_io` 的失败路径。

`cmake --build build --target fstest-bare-tests` 把 `tests/*.c6` 的 test 块搬上裸机：35 个文件、全部
test 块、0 失败，每个文件一个镜像（`examples/boot/runtests.sh`）。test 块到
`__tcc_test_<n>` 的对应表只存在编译器状态里，二进制里没有，所以由
`examples/boot/harness.py` 按源码顺序扫出来生成入口；libc 那层由
`examples/boot/shim.plain.c` 提供（printf/字符串/qsort/内存管道/select/桩子
+ 4MB 首次适配分配器）。跳过 7 个：`thread`/`thread_handle`/`gc_thread`/
`gc_drop`/`tuple_thread` 要 `pthread_create`，`defer_thread` 要 `clone`/`futex`，
`method` 要 libc 的 `FILE` 布局。整个文件跳太重时只跳一块：块上一行写
`// BARE-SKIP`，harness 就把它从表里去掉、块号不变（`tests/generic_fn.c6`
里起线程那块，同一个文件剩下 8 块仍跑）。条件编译里没开的块不用标：harness
认 `#if`/`#ifdef`（裸机编译不带 `-D`，`#if defined X` 一律当假），这种块既不
进表也不占号（`tests/test-block.c6` 的 `#if defined FAILING_BLOCK`，否则表里会
多出 `__tcc_test_7` 这个悬空引用）。踩到的三件事：默认 256KB 的 bump
arena 不够（给 calloc/free/realloc 就好）；`select` 要把 0/1/2 当永远就绪；
多个 TU 一次编译会撞上 `c6_gc_new_lead` 的 NULL 标签名 strcmp（拆成 -c + 链接绕开）。

"spawn 在裸机里是真并发吗" —— 现在能回答"是"，代价是自己写了个最小内核。
`examples/boot/sched.plain.c`：8259A 重映射到 `0x20`、8254 每 10ms 一次、
中断进来先换到调度器自己的栈（`istack`），把现场摆成一块 160 字节的
`struct Frame`（15 个通用寄存器 + `rip/cs/rflags/rsp/ss`），换任务 = 换块
Frame；新任务的帧是编出来的，第一次被调度时 `iretq` 直接落进 `task_entry`。
对 `libtcc1` 暴露的就那 8 个 `pthread_*` 弱符号。`cmake --build build --target fstest-bare-go` 里
两个**没有让出语句**的任务互相等 flag，空转 400 万次（上限 2 亿）就看到对方
立起来的 flag —— 顺序执行得把 2 亿次跑完，所以这是真的被时钟抢占交错了。

三个只有真跑才知道的：长模式下 `iretq` **总是**弹五个 qword，而中断进来时
CPU 压了几个得看机器 —— 这台（KVM）压五个，被打断的 `rsp` 就明明白白记在
`[入口 rsp + 24]` 那一格里，直接读它，千万别自己算"入口 rsp + 24"（偏低
16 字节，见下面那条）；`boot.S` 里 `0x18`
才是 64 位代码段（中断门 selector 写 `0x08` 第一次时钟中断就 `#GP(0x08)`，
表现成三重故障、qemu 退出码 0，像"什么都没发生"）；引导器原来的栈
`0x80000` 其实落在 payload 的 `.bss` 里面，变量一多就踩栈（这次踩到的是
中断栈和堆），已挪到 16MB。

**SMP：两个核真的在同时跑。** 现在 `cmake --build build --target fstest-bare-smp` 会做两件事 ——
`SMP=2` 起两个核、`SMP=1` 起一个核做负对照。两台都要打同样的判定：

- 每个任务自己报"我在哪个核"，核号从 LAPIC 的 ID 寄存器（`0xFEE00020`）读；
- A 把**本核中断关掉**再空转两百万圈：这段时间里引导核不可能切到别的任务，
  可 B 的计数器还在涨 —— 那只可能是另一个核在跑它。

```
smp: 在线核数 2，A 跑在核 0
smp: B 跑在核 1（它自己从 LAPIC 读的）
smp: A 关中断空转期间 B 的计数器涨了 1089907
smp: 真并行 —— 两个任务同时跑在两个核上
```

`SMP=1` 时同一个程序打印的是"B 在核 0、涨了 0、只有交错没有并行"——没有这
半张负对照，"并行"就只是个说法。

怎么起的 AP：`examples/boot/ap.S` 是 16 位实模式跳板（tcc 的汇编器不认
`.code16`，所以走 binutils：`as --32` + `ld -Ttext 0x8000`，222 字节）。
`build.sh` 用 `objcopy -I binary` 把它包成目标文件链进 payload。引导核往三个
低内存邮箱写好 cr3（`0x7000`）、AP 入口（`0x7008`）、每个 APIC ID 的栈顶
（`0x7010`），然后对每个 AP 走 INIT → 等 10ms → SIPI 0x08 → 等 10ms →
SIPI 0x08。跳板加电后自己 lgdt、进保护模式、打开 PAE/长模式、读自己的
APIC ID、挑栈、`call` 进 `ap_main`。

分工：引导核继续用 8254 + 8259A 的时钟抢占调度（就是单核那条路，一点没动），
AP 不装 IDT、不开中断，只是不停地从任务表里捡 `T_READY` 的活，捡到就直接
把函数调起来跑完。共享的任务表用 `sched_xchg` 做自旋锁，状态机
`T_READY/T_RUNNING/T_DONE` 保证同一个任务不会同时被两个核选中（"正在别的核
上跑"是调度器挑下一个任务时明确跳过的一类）；有多核时新任务标 `ap_only`，
留给 AP——单核启动时这个标记自动失效，于是又回到"引导核自己跑"。

这一轮踩到的坑，两个是硬伤级的：**启动 AP 期间必须 `cli`** —— 时钟插进
`delay_10ms` 那种短函数里，Iretq 恢复现场有极小概率落进栈地址（表现为
`#UD` at `0xffff..`），关中断就彻底回避了；**AP 上绝不能 `sti`** —— 它没装
IDT，开中断等于三重故障。另外 `boot.S` 得把 LAPIC 那页（`0xFEE00000`，属性
`0x93` = PCD|PWT|present，不能走 cache）先映射好，否则第一次访问就 `#GP`。

`fstest-bare-smp` 之前几跑一偶发三重故障（引导核侧），根因就是上面那条：
`sched_irq0` 拿"入口 `rsp` + 24"当被打断的 `rsp`，可这台机器上 CPU 压的是
五个 qword、真值在 `[入口 rsp + 24]`，算出来偏低 16 字节。被打断的函数带
着偏低的 `rsp` 继续跑，rbp 寻址的代码全都自己修回来了，所以绝大多数时候看
不出问题；只有 `ret` 弹到栈上才炸成 `#UD at 0xffff..`。而 `boot_putc` 那条
`out %al,(%dx)` 正好是一次 I/O 退出，挂起的时钟中断就是在那儿注入的 ——
于是中断十有八九插在紧跟其后的 `ret` 上，这个偶发才显得那么"随机"。现在
改成直接读硬件压下的 `rsp`，同一镜像连跑 40 次不再复现。

`spawn` 的**句柄**（`h->join()`/`h->value()`）在裸机上已打通：它走的还是
`libtcc1` 的每线程块（`calloc`/`free` + pthread 键），而 `sched.plain.c` 把
这 8 个 pthread 入口都给了最小实现（`pthread_join` 就是自旋等任务自己的
`done`）。用 `thread h = spawn twice(21); h->join(); h->value();` 在裸机上实测
是 `join=0 value=42`，任务体确实跑过（当初"任务收尾后三重故障"是调度器读
被打断 `rsp` 那个 bug 的连带表现，那个修掉之后就不复现了）。
