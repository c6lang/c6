# c6 语言扩展实现方案（深度评审修订版）

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
| **G8** | `#macro` 的 `__local` 重命名通过 `TokenSym` 插入唯一名，但 TCC 的 `TokenSym` 是全局 intern 表，宏展开产生的临时标识符会永久累积 | 中 | 重命名使用**展开计数器后缀**（如 `tmp__h17`），但不在 `TokenSym` 中持久化；展开结束后从 intern 表移除，或接受可控的内存增长（首版可接受，后续优化） |
| **G9** | 包管理的 `package NAME { ... }` 在 `tccgen.c` 顶层处理，但 TCC 的顶层声明循环 (`decl`) 不天然支持“命名块”语法 | 中 | 将 `package NAME { ... }` 解析为**语法糖**：读取包名后进入特殊声明上下文，对块内每个全局符号在注册时施加 `NAME__` 前缀；块结束后恢复。不引入真正的“顶层块作用域”，只做 mangle |
| **G10** | `err` 的错误槽“通过隐藏参数传递”在 TCC 的调用约定中无现成支持 | 中 | 使用**每线程错误槽**（`pthread_key` + `pthread_getspecific`，不是 `__thread`：`-run` 不支持 TLS），调用者从槽读取；`go` 出去的线程有自己的槽，跨线程传播需手动复制。隐藏参数作为后续优化 |


## 1. 总体架构与扩展开关

### 1.1 挂载点

| 源文件 | 职责 | 扩展挂载点 |
|---|---|---|
| `tcctok.h` | 关键字与 token 编号 | `#ifdef C6_EXT` 注册新 token；关闭时零映射 |
| `tccpp.c` | 预处理与词法分析 | `#macro` 指令识别、`__local` 重命名、include 路径扩展 |
| `tccgen.c` | 语法分析与代码生成 | `package/import` mangle 与 `pkg.member`、结构体方法注册、`interface` 转换与 vtable 生成、`defer/errdefer/err` 就地代码生成、`test` 块注册 |
| `tcc.h` / `libtcc.c` | 符号表与编译状态 | `TCCState` 扩展字段、`-test` runner、扩展开关 |

### 1.2 扩展开关（修复 G7）

```c
/* tcctok.h */
#ifdef C6_EXT
DEF(TOK_PACKAGE,   "package")
DEF(TOK_IMPORT,    "import")
DEF(TOK_INTERFACE, "interface")
DEF(TOK_DEFER,     "defer")
DEF(TOK_ERRDEFER,  "errdefer")
DEF(TOK_ERR,       "err")
DEF(TOK_TEST,      "test")
#endif
```

- 开关：`-std=c6`，默认关闭。
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
    TccThRec  *th_list;    /* 本线程 go 出去的线程 */
};
```

`tcc_th()` 用 `pthread_getspecific` 取当前线程的 `TccThCtx`，第一次用时
`pthread_key_create` 建键并分配；`go` 出去的线程继承不到启动方的上下文，
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


## 6. 包管理：mangle 语法糖 + 符号表扩展（修复 G9）

### 6.1 语法

```c
package math {
    int add(int a, int b) { return a + b; }
    int counter = 0;
}
import math;

int main(void) { return math.add(1, 2) + add(1, 2); }
```

包名可以是多级的，且三种写法等价（前缀都是 `math__util`）：

```c
package math.util { ... }
package math { package util { ... } }
package math.util;                 /* 文件级：命名文件的其余部分 */
import math.util;
```

### 6.2 实现（修复 G9）

`package` 不引入真正的顶层块作用域，而是**mangle 语法糖**：

1. 解析 `package NAME`：在 `TCCState` 中设置 `current_package = "NAME"`；点号与嵌套
   都只是前缀组合（`a.b` → `a__b`，`a { b { } }` → `a__b`）；
2. 块内每个全局符号注册时，符号名施加前缀 `NAME__`（如 `math__add`），`main` 除外；
3. `import NAME;`：把包名登记进 `c6_imports`，不作为声明、不打开文件；
4. 块结束恢复 `current_package = NULL`；`package NAME;` 命名文件其余部分。

`pkg.member`（可多级）由 `unary()` 解析：先按普通 C 标识符查找，命中可见对象则保持
C 的成员访问语义；未命中且后随 `.` 时逐段增长前缀，用 `sym_find_raw(pkg__seg)` 查找，
命中且该包已 import（或就是当前包）才接受，剩下的 `.field` 交回普通 postfix 路径。
未 import 时报 `'pkg' is not an imported package (use 'import pkg;')`。

首版**不导出宏**；typedef/tag 命名空间按包隔离；`static` 保持文件内可见。

### 6.3 跨文件

没有包清单：跨 TU 就是同名 mangle 的普通 C 符号。头文件里用
`package NAME { ... }` 写出声明（`extern` 也行），实现在另一个 TU 的同一个包里；
老 C 代码想调用包导出，手写 `int math__add(int, int);` 即可，反之扩展 TU 也能直接
调用普通 C 函数。`import` 至少打开源码里的 `.` 写法；从 6.5 起，如果能找到同名
模块文件，还会加载它的声明（找不到时仍是纯命名空间，不引入文件语义）。

### 6.4 结构体方法

`ret f(struct T *self, ...)` 在文件作用域注册为 `T` 的方法，符号名是
`<T>_f`；`p->f(a)` 被改写成 `T_f(p, a)`（接收者先存临时槽，返回结构体时排在
sret 缓冲之后）。源码里写的 `f` 另外输出成一个**弱别名**，所以普通调用
`f(p, a)`、手写 C 和纯 C 翻译单元照旧能链接，而不同结构体可以用同名方法而
不必放进不同包：

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
方法（匿名 tag 的编号是每个 TU 各一份）。包内声明的方法保持 `pkg__name`，
包已经起了区分作用，`import` 后照样 `p->f()`。

字段优先，方法须先于调用点声明，`duplicate method` 检查按 (类型, 源码名) 做
（一个类型仍然只能有一个同名方法）。方法表与包 mangle 正交：包内定义的方法
仍然用源码里写的名字注册。

### 6.5 模块：`module` + 声明加载

`package` 作用于一段代码，符号在源码里仍要写限定名；`module` 作用于**整个
文件**，源码继续写短名：

```c
/* math.c */
module math;
struct Point { int x, y; };
typedef struct Point Point;
int add(int a, int b) { return a + b; }
int area(Point *p) { return p->x * p->y; }     /* math__Point_area */

/* main.c */
import math;
int main(void) { Point p = {3, 4}; return add(1, 2) + area(&p); }
```

1. `module NAME;` 是文件的第一条声明（软关键字：文件里已有同名普通标识符时
   按 C 语义处理），设置 `BufferedFile.c6_module = "NAME__"`；
2. 文件作用域的声明经 `c6_mod_name` 加前缀：普通名字 `NAME__sym`，方法
   `<NAME__Tag>_<meth>`；块作用域、`static`、`test`、`__` 开头和 `#include`
   的普通 C 文件不改名；模块文件不发方法的原名弱别名，所以 C 侧只看到前缀名。
   类型位置也接受 `NAME.Type`（多级点号同理）：`parse_btype` 先做一次可回退
   的扫描（`c6_qualified_type`，先 `sym_find` typedef 再 `struct_find` tag），
   解析到 `NAME__Type`（module 的 tag 有前缀；package 的 tag 仍是普通 C tag，
   所以 package 限定类型要写 typedef）；不是类型时把 token 原样还回，所以
   `pkg.func(...)` 这类表达式语句仍走普通表达式解析；
3. `import NAME;` 在原有“登记命名空间”之外，按当前文件目录 → `-I` →
   当前目录查找 `NAME.c`（点号形式 `a/b.c`），找到就用 `decl(VT_CONST)`
   重放声明：函数只登记原型（`skip_or_save_block` 跳过函数体），全局变量只
   登记 `extern`（`c6_skip_decl_tail` 跳过初始化器），`static` 对象和 `test`
   块跳过；文件必须声明 `module NAME;`，循环 import 报错，找不到文件时保持
   旧的命名空间行为；
4. `-emit-header FILE` 在模块 TU 末尾把记录下来的导出（`C6HeaderDecl` 数组）
   写成普通 C 头文件：结构体/枚举/typedef/函数/全局变量，切片和元组生成
   `NAME__slice_N` / `NAME__tuple_N` typedef，C 侧包含头文件即可调用。

跨 TU 仍然是普通 C 符号；`#include` 的普通头文件按 C 规则编译，只有模块文件
自己的文件作用域声明加前缀。这是“零运行时、零新链接器特性”的方案：符号表里
没有模块元数据。


## 7. 卫生宏：`#macro` + `__local`（修复 G8）

### 7.1 语法

```c
#macro swap(a, b) {
    typeof(a) __local tmp = (a);
    (a) = (b);
    (b) = tmp;
}
```

`#` 前缀作为预处理指令识别。`__local` 标注需要卫生重命名的标识符；宏参数和全局符号不重命名。

### 7.2 展开（修复 G8）

在 `macro_subst()` 中：

1. 展开计数器自增得 `N`；
2. 对每个 `__local ident` 生成唯一名 `ident__hN`，构造新 token 注入展开流；
3. **不在 `TokenSym` 中持久化**临时名，避免 intern 表膨胀；展开结束后丢弃；
4. `#`/`##` 用于 `__local` 标识符时首版报错；
5. 递归展开沿用现有机制，每层独立计数。

### 7.3 限制

首版只支持显式 `__local`；不支持宏体引入新 `#define`；错误消息映射回原始行号。


## 8. 分阶段实施计划

| 阶段 | 内容 | 关键验收 | 依赖 |
|---|---|---|---|
| **0** | 语义规范、`C6_EXT` 条件编译、测试矩阵 | 扩展关闭零行为变化 | — |
| **1** | `defer`：就地生成清理块 + 运行时记录链表 + `ScopeTracker` | 条件/循环/递归/多 defer 逆序正确；`goto` 跨作用域清理；返回值落栈保护 | 0 |
| **2** | `errdefer + err`：线程局部错误槽 + 就地清理 + 哨兵传播 | 多层传播逐层执行 errdefer；递归不串错误 | 1 |
| **3** | **`test "desc" {}`**：顶层注册、`-test` 模式、`--test-filter`、与 `err` 集成 | PASS/FAIL 正确；退出码正确；后续特性用 `test` 验收 | 2 |
| **4** | 包管理单文件：`current_package` mangle、import 重定向 | 符号可见性/冲突正确；不导出宏 | 3 |
| **5** | `interface`：胖指针 ABI、结构化实现、vtable 生成 | 方法先于转换点；签名检查；用 `test` 验收 | 3 |
| **6** | 跨文件包 + 跨 TU interface：包清单、`extern` vtable 声明 | 跨 TU 链接正确 | 4, 5 |
| **7** | 卫生宏：`#macro` + `__local`、展开计数器重命名 | 正反例通过；用 `test` 验收 | 3 |

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
| 变量初始化 | 各类定义带初始化器（标量/数组/结构体/`static`/`for` 声明） | 自动变量、全局、`static` 局部/全局缺初始化器报错 | VLA 豁免、`extern` 声明/形参/成员/`typedef` 不受限、`package` 内报原名 |
| 裸 tag 类型名 | 声明/形参/返回/成员/强转/`sizeof`/切片元素/方法接收者/typedef | tag 被普通符号（函数/变量/`stat`）遮蔽、tag 未声明、tag 出作用域、局部 tag 的切片、不完整类型 | 语句开头 `TAG:` 标签、`TAG(...)`、`const`/`typedef` 组合、`typeof`/`_Generic`/`_Alignof`、接口方法签名、跨 TU |
| 元组 `(T1, T2)` | 返回值/解构声明/解构赋值/`_`/`t[k]`/参数/成员/数组元素/嵌套/复合字面量/`sizeof`/`async`+`await`/`go`/`!error`/跨 TU/与 struct 的 ABI 互通/单条语句体与语句表达式里的解构 | 元素是 `void`/函数/VLA/数组/不完整/局部 tag/`error`、下标非常量或越界、解构个数不符、混写类型与名字、全 `_`、右边不是元组、非左值目标、`return {…}` 用在标量函数、`float!error` | `#pragma pack` 不影响布局、`(a, b)` 逗号表达式与各种强转/复合字面量不被误判、`_` 别处仍是普通标识符、文件作用域的元组对象 |

### 9.2 工程要求

- 现有 TCC 测试套件在 `C6_EXT` 开/关两种模式下全回归；
- 调试信息保留原始源位置（尤其 `defer` 清理块和卫生宏）；
- `TCCState` 扩展字段保持 ABI 兼容；
- `__tcc_err` 是每线程槽（`pthread_key`，不是 `__thread`）：`go` 出去的线程有自己的错误槽，跨线程传播要手动复制；
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

构建与运行：

```bash
cd tcc && make clean && make -j$(nproc) EXTRA-DEFS=-DC6_EXT CFLAGS="-g -O0"
tcc/tcc -std=c6 -B. -run file.c        # 扩展开启
tcc/tcc -std=c6 -B. -test file.c       # 运行 test 块
```

| 阶段 | 状态 | 关键实现位置 |
|---|---|---|
| 0 `C6_EXT` 门控 | ✅ | `tcctok.h`（新 token 追加在文件末尾，避免破坏 i386 asm 关键字）、`libtcc.c`（`-std=c6`）、`tccpp.c`（`__C6__`、未开启时把新关键字还原为普通标识符） |
| 1 `defer` | ✅ | `tccgen.c`：`c6_defer_parse` / `c6_defer_begin` / `c6_defer_func_end`（运行时记录栈 + 每函数调度器）、`lib/libtcc1.c`：`__tcc_defer_*` |
| 2 `errdefer`/`err` | ✅ | `c6_errdefer_parse` / `c6_err_parse`、`__tcc_err_set/get/clear`；`err` 直接返回哨兵 `-1`，转发由 `try` 负责 |
| 2b `T!error` / `try` / `catch` | ✅ | `c6_parse_fallible_suffix` / `c6_apply_fallible`（`FuncAttr.func_fallible`）、`c6_try_parse`（`unary()` 前缀分支）、`c6_catch_parse`（`expr_catch()` 层）、`c6_emit_propagate`（与 `err` 共用清理+哨兵路径）、`__tcc_error_take` / `__tcc_err_flag_peek` |
| 3 `test` / `-test` / `-test-filter` | ✅ | `c6_test_parse`、`tccrun.c:tcc_run_tests`、`libtcc.c` 选项 |
| 4 `package`/`import`/`pkg.member` | ✅ | `c6_package_parse` / `c6_import_parse`、`c6_pkg_name` / `c6_pkg_compose` / `c6_pkg_imported`、`c6_mangle_tok`（`NAME__sym`）、`unary()` 里的逐段点号解析与 `sym_find_raw` |
| 4b 结构体方法 | ✅ | `decl()` 里 `c6_method_register`、`c6_method_symbol`（符号名 `<Tag>_<name>`，`static`/`inline`/`__` 开头/系统头/匿名 tag/包内方法不改名）、`c6_method_plain_decl` + `c6_method_alias`（源码名发布成弱别名，定义处用 `put_extern_sym2` 打上同一地址）、`c6_method_source_name`（`__func__` 显示源码名）、`c6_method_lookup` / `c6_has_field` / `c6_method_call_prep`、`unary()` 的 `->` 分支、`c6_self_*` 隐藏接收者（sret 之后）；`BufferedFile.sys_header`（`tccpp.c` 按 include 路径标记系统头） |
| 5 `interface` | ✅ | `c6_interface_parse`（生成 `struct NAME_vt` + 胖指针 `struct NAME`）、`c6_auto_impl_ensure` / `c6_check_method`（惰性校验）、`c6_emit_auto_impls`（TU 末尾生成 `static` vtable）、`c6_iface_call_prep`（调用点改写）、`c6_iface_convert`（隐式/显式转换） |
| 6 跨 TU | ✅ | 头文件用 `package NAME { ... }` 声明 API；接口与方法声明放头文件，每个 TU 各自生成 vtable；方法符号按接收者取名，所以同名方法跨 TU 也不需要包；`c6_tu_reset` 在每个 TU 开头丢弃上一 TU 的包/接口/方法/待生成 vtable |
| 7 `#macro` + `__local` | ✅ | `tccpp.c`：`parse_c6_macro` / `c6_hygiene_body`（展开计数器 `ident__hN`，在参数替换前重命名，`#`/`##` 误用报错） |
| 10 变量必须初始化 | ✅ | `tccgen.c`：`c6_check_var_initialized`，在 `decl()` 的对象定义分支（`has_init` 判定之后、TCC 把暂定全局改成 `extern` 之前）调用；`extern`/函数/`typedef`/`VLA`/alias 豁免；诊断使用 mangle 前的原始标识符 |
| 11 切片 `T[:]` | ✅ | `parse_btype()` 的 `case '['`（只认 `[:]`，其余 `unget_tok` 还原）→ `c6_slice_get`（按元素类型规范化生成内部 struct，固定布局、不走 `struct_layout`，因此 `#pragma pack` 无效）；`c6_slice_subscript`（`unary()` 的 `[` 分支，下标与区间）、`c6_slice_spill`（把基值落到本地 `{data, len}` 对）、`c6_slice_convert`（数组→切片：`vstore`/`gen_assign_cast`/强转/`decl_initializer`）；`type_to_str` 打印 `T[:]`；运行期 `libtcc1.c` 的 `__tcc_slice_index` / `__tcc_slice_sub`（越界空指针写陷阱） |
| 12 `async`/`await` 无栈协程 | ✅ | `parse_btype()` 的 `TOK_C6_ASYNC`（`FuncAttr.func_async`）→ `c6_async_declare`（用合成 token 在文件作用域声明 `struct NAME_task`）；`c6_async_definition` 把函数体按 `int __c6_body_NAME(void *task)` 编译，`c6_async_func_begin` 算帧基址并分发、`c6_async_params_copy` 把形参落成普通局部、`c6_async_func_end` 生成恢复分发器、`c6_async_finish` 写 `val` 并返回 `KIND_DONE`；`c6_async_call`（`unary()` 的 `tok_identifier` 分支）把调用改写成 `__c6_co_new` + 实参拷贝；`c6_await_parse`（`unary()` 前缀分支）+ `c6_async_suspend` 把帧/defer 记录/错误槽 park 出去；运行期 `libtcc1.c` 的 `__c6_co_new` / `__c6_co_await` / `__c6_co_suspend` / `__c6_co_restore` / `__c6_co_finish` / `__c6_co_wait` / `__c6_co_destroy`（固定池 + 手写 `select()`，不依赖 libc）；预置声明在 `tccpp.c` 的 `__TCC_ASYNC_DEFINED` 段 |

| 13 `co` / `task` 软关键字 | ✅ | `co`/`go`/`task` 不进 `tcctok.h`：词法阶段仍是标识符，`block()` 里 `tok >= TOK_IDENT` 且 `c6_soft_kw(t)` 命中、名字没有被 `VT_TYPEDEF` 占用时按关键字分派（`c6_soft_kw_shadowed`），所以 `int go = 1;`、`go(x);`、`typedef int co;`、`go:` 都还成立；`c6_co_parse`（`decl()` 与 `block()` 两个入口——单条语句不走 `decl()`）+ `__tcc_co_spawn`；`c6_task_decl` / `c6_task_check_handle`（`struct NAME_task` 的 `head` 必须是 `struct __c6_task`）/ `c6_task_defer_destroy`（复用 `__tcc_defer_push` 注册作用域清理）；运行期 `libtcc1.c` 的顶层就绪链表（`snext`/`busy`/`waiting`）与 `tcc_co_pump` / `tcc_co_step_once` / `tcc_co_link` / `tcc_co_unlink` / `__c6_co_run`，`__c6_co_wait` 也改成先推进顶层任务；`async` 体里的 `err` 走 `c6_async_propagate`（`KIND_ERR`），并禁止 `async` 方法是方法；预置声明同步加 `__tcc_co_spawn` / `__c6_co_run` |
| 14 `go` = OS 线程 | ✅ | `c6_go_parse` 只在 `-std=c6` 下生效：实参按声明形参布局拷进 `__tcc_sp_args(size)` 的堆块（对齐循环与跳板共用同一套 `align`），跳板 `static void *__c6_sp_NAME(void *)` 在 `go` 调用点用 `external_sym(VT_STATIC\|VT_EXTERN)` 惰性声明、由 `c6_sp_flush()` 在 TU 末尾定义（`c6_sp_pending` 链表，`c6_tu_reset` 释放），所以头文件里的原型不产生任何代码；运行期 `libtcc1.c` 新增 `__tcc_th_start`（`pthread_create` + 把 `TccThRec` 挂到本线程链表）、`tcc_th_tramp`（解开参数块、调用、释放）、`__c6_th_join_all` / `tcc_th_join_children`；`gen_function` 在 `main` 末尾插 `__c6_th_join_all()`，`tccrun.c:tcc_run_tests` 也调一次，`-run` 不会在线程还在跑时释放 JIT 内存；每线程状态（defer 栈 / 错误槽 / 协程池）从 `__thread` 改成 `pthread_key_create` + `pthread_getspecific`（`-run` 不支持 TLS） |
| 15 裸 tag 类型名（省略 `struct`） | ✅ | `tccgen.c`：`parse_btype()` 的 `default` 分支——`typespec_found` 为 0、`sym_find(tok)` 未命中（普通命名空间优先）时试 `struct_find(tok)`，命中则把 tag 符号的 `type.t` / `ref` 交给 `basic_type2`，与 `struct/union/enum TAG` 走完全同一条路径（所以 `sizeof`、方法注册、`c6_tag_is_global` 的切片元素检查、`type_to_str` 自动一致）；`ignore_label` 时 `TAG:` 仍退回标签（与 typedef 路径同一个 guard）；不隐式前置声明，也不支持定义处省略 `struct`；只在 `-std=c6` 生效 |
| 16 `foreach` | ✅ | `c6_foreach_parse`（`block()` 的 `TOK_C6_FOREACH` 分支；`tok != '\|'` 时 `unget_tok` 退回表达式语句，所以 `foreach` 仍可当标识符）；词法是 `tcctok.h` 末尾追加的硬关键字，`-std=c6` 关闭时由 `tccpp.c` 的还原循环改回普通标识符（区间同步扩到 `TOK_C6_FOREACH`）；数组/切片的隐藏槽直接复用 `c6_slice_spill`（`{data, len}` 对，与切片运行期布局相同），区间把上界落进隐藏整数槽、下标绑定当计数器；`i`/`v` 是 `sym_push` 出来的普通局部，循环体走 `lblock`，所以 `break`/`continue`/`defer`/`await` 与 `for` 一致；`size_t` 下标用 `c6_size_type`，区间用 `c6_common_int_type`（C 的通常算术转换子集）；文件作用域的 `foreach \|…\|` 在 `decl()` 里报错 |
| 17 元组 `(T1, T2, …)` | ✅ | `tccgen.c`：`parse_btype()` 的 `case '('` → `c6_tuple_btype` / `c6_tuple_speculate`（保存/重放 token，区分元组类型与老 C 的括号表达式）、`c6_tuple_get`（校验元素并规范化生成内部 tag `__c6_tuple_N`，布局在构造时按自然对齐算好，不走 `struct_layout`）、`c6_tuple_ctype` / `c6_tuple_of_type` / `c6_tuple_ctype`；`unary()` 的 `case '('` 处理元组强转/复合字面量/`sizeof`/`_Alignof`、`c6_tuple_subscript` 处理 `t[k]`（常量下标）；`decl()` 开头的 `c6_tuple_lead_kind` + `c6_tuple_destructure` 实现解构声明/赋值（`_` 占位符丢弃元素）；`c6_return_value` 支持 `return {…};`；`c6_apply_fallible` 放开 struct/union/元组作 `!error` 返回类型，失败路径用 `c6_zero_local` 清零；`c6_async_type_tokens` 把元组回放成 `(T1, T2)` 而不是内部 tag；解构语句在 `block()` 里也认一次（无花括号的 if/while/for/switch 单条语句体、语句表达式、`defer` 单条语句体不走 `decl()`），赋值形式在 `c6_tuple_destructure()` 里就地解析（作用域弹出后隐藏临时变量就没了） |
| 18 `module` + `import` 加载 | ✅ | `tccgen.c`：`BufferedFile.c6_module` / `c6_decl_seen`，`decl()` 文件作用域的 `module NAME;` 软关键字 → `c6_module_parse`（点号前缀、必须首条声明、一个文件一个、`main` 禁止）；`c6_mod_name()` 给文件作用域声明加 `NAME__`（块作用域/`static`/`test`/`__` 开头/普通 `#include` 不加），方法经 `c6_method_symbol` 自然变成 `<NAME__Tag>_<meth>`；`sym_find` / `struct_find` 先试模块前缀再试普通 C 名，`c6_import_lookup` / `c6_import_tag_lookup` 兜底 imports；`parse_btype` 的 `c6_qualified_type` 支持 `NAME.Type`（先 speculative 扫描 typedef/tag，失败原样回退）；`c6_module_import` 按当前文件目录 → `-I` → 当前目录找 `NAME.c`（点号 `a/b.c`），用 `decl(VT_CONST)` 重放：函数原型 + `skip_or_save_block`、全局 `extern` + `c6_skip_decl_tail`、跳过 `static`/`test`，循环 import 报错，找不到文件保持旧的命名空间行为；`-emit-header` 用 `C6HeaderDecl` 记录导出，`c6_hdr_type` / `c6_hdr_struct_body` / `c6_emit_header` 写出普通 C 头文件（切片/元组生成 `NAME__slice_N` / `NAME__tuple_N` typedef） |
| 19 托管引用 P0/P1（限定符、分配、存储、释放） | ✅ | `tcc.h`：`CType.gq` + `C6_GQ_*` + `C6_GCM_*`；`tccgen.c`：`parse_btype()` 的 `@` 前缀、`c6_gc_new_expr`、`c6_gc_store_hook`（`vstore()` 里）、`c6_gc_site`（复用 defer 记录）、`c6_gc_plain` / `c6_gc_mode` / `c6_type_gq`；运行期 `libtcc1.c` 的 `__c6_gc_new` / `__c6_gc_store` / `__c6_gc_release` / `__c6_gc_drop`；`CType.gq` 只当缓存、可信副本在 pointee 上（设计 §2.0） |
| 20 描述符与字段释放 P2 | ✅ | `c6_gc_collect()`（struct 定义完成时收集字段表：`C6GcField.off/kind/count/stride`）、`c6_gc_size_of()`/`c6_gc_desc_for()`/`c6_gc_desc_decl()`/`c6_gc_emit_desc()`/`c6_gc_flush_descs()`（TU 末尾发射 `static const struct __c6_gc_desc __c6_gc_d<fnv>_<n>`，跨 TU 不重名）；运行期按描述符逐字段释放（数组逐元素、嵌入递归） |
| 21 `weak` P3 | ✅ | 弱槽注册表（全局自旋锁，冷路径）+ `__c6_gc_weak_add`/`__c6_gc_weak_unlink`/`__c6_gc_weak_store`（原子 try-incref 升级）；`weak` 不能直接解引用（`vstore`/`unary` 两处检查）；async 里禁 `weak`（`c6_async_in_body`） |
| 22 析构 `drop` P4 | ✅ | `c6_gc_drop_register()`（`c6_method_register()` 里登记）、`c6_gc_is_drop()` / `c6_gc_drop_callee()`（禁止 `p->drop()` / `drop(p)`）、描述符增加 `drop` 指针、运行期 `c6_gc_free_one()` 先跑析构再放字段；全局根用 TU 末尾的 `__attribute__((constructor))/((destructor))`（不用 `atexit`，否则拖出 `__dso_handle`） |
| 23 `shared` 与跨帧/跨线程 P5 | ✅ | 原子与否在对象头（`new shared T`），`__c6_gc_inc`/`c6_gc_drop` 按标志走原子指令；`go` 实参块 +1 / trampoline −1（`c6_sp_emit`）、`async` 任务前缀 +1 / `__c6_co_destroy` −1（描述符第 5 个成员 `release` → `__c6_rel_NAME`）；形参是借用（`C6_GQ_BORROW`，E-GC14）；`new shared T` 强制（E-GC15） |
| 24 静态环检测 P6 | ✅ | `c6_gc_collect()` 记 `C6GcField.to_tag`（强引用边、含数组与按值嵌入；`weak`/`own` 不是边），`c6_gc_flush_descs()` 里三色 DFS（`C6GcType.mark`）报 E-GC1；`move(p)` → `__c6_gc_move`（`own` 链“先建后接”的唯一写法）；`move(p)` 之后的空槽再用报 E-GC17（`Sym.gc_moved` + `if`/`while`/`for`/`switch`/`foreach` 的状态合并、`defer` 体隔离，`tests/errors/gc_move.c` 8 条） |
| 25 任务回卷 P7 | ✅ | 运行期 `C6_CO_KIND_ABORT` / `__c6_co_abort` / `__c6_co_destroy` 先回卷；编译器在**每个 `await` 的恢复点**发 `if (__c6_co_kind(task) == ABORT) { defer_begin(2,0); finish(ABORT); }`（放末尾派发器里的话 `defer_state` 已复位，清理体发不出来） |
| 26 线程与任务句柄 | ✅ | `thread h = go f(x);`（`c6_thread_decl` + `c6_thread_defer_release`，运行期 `TccThRec` 引用计数：链表 + 句柄）、`h->join()`/`h->done()`/`h->value()`；任务句柄方法 `t->wait()`/`t->done()`/`t->kind()`（预置方法 + `c6_task_head_ptr` 首字段上转）；`c99-rt.inc` 同步镜像、`tests/thread_handle.c`、`tests/task_method.c` |

`foreach` 的设计取舍（评审结论）：

- **绑定在前**（`foreach |i, v| a`）而不是 Go 风格的 `for a |i, v|`：`for`
  必须保持纯 C，而 `|` 本来是按位或的记号；把绑定表放在可迭代对象前面，
  表达式中间就不必再猜“这个 `|` 是绑定表的开始还是按位或”；
- **不做运行期迭代协议**（P1）：方法符号虽然已经按接收者改名（§6.4），
  `has`/`len`/`at` 不再和用户函数撞名，但 `void *next()` 会擦掉元素类型，
  每轮都得装箱/分配；可迭代对象限定为数组、切片和区间，其它类型走
  “暴露切片成员”，语义完全静态；
- **`lo:hi` 而不是 `0..n`**（P14）：`0..3` 在词法阶段是两个浮点常量
  （`0.`、`.3`），为它改词法会波及现有代码；`:` 在表达式里本来只出现在
  三目运算符里，而可迭代对象按 `conditional-expression` 解析，三目的
  `:` 已经被吃掉，所以 `lo:hi` 没有歧义；
- 隐藏槽复用切片运行期布局，因此 `foreach` 不带运行期辅助函数、不改变
  调用约定；`async` 体里照常可用（帧整体保存/恢复）。

元组的设计取舍（评审结论）：

- **不加关键字**：类型写成 `(T1, T2)`，和现有括号语法共用前缀。解析器在
  `parse_btype()` 的 `case '('` 处做一次**投机 + 回滚**（`c6_tuple_speculate`：
  `begin_macro` 重放 token，`type_decl(TYPE_ABSTRACT|TYPE_DIRECT)` 走一遍，
  成功且右括号前没有名字才算元组），失败就原样退回普通括号。因此
  `(void)printf(…)`、`(int)x`、`(struct S){…}`、`({…})`、`(a, b)` 全部零变化；
- **整组扫描要把 `sizeof(type)` 的 `(` 当成开括号**：`sizeof` / `_Alignof`
  后面跟 `(` 时，`unary()` 拿到的是 `TOK_SOTYPE`（`'('` 的别名）。扫描器只数
  真括号的话，匹配的 `)` 会把深度减到 -1，组永远不闭合，一路吞到 `TOK_EOF`；
  这时回放的 token 已经跨过文件尾，行号来自 token 串里的行标记、文件名却来自
  弹出后的包含者，诊断会报成“源文件:头文件里的行号”。`tests/errors/loc.c`
  守这条；
- **不上新 ABI**：元组就是 `VT_STRUCT` + 内部 tag `__c6_tuple_N`，参数和返回值
  都走已有的 struct 路径（`gfunc_sret` / `classify_x86_64_arg`），`libtcc1.c`
  一个新符号都没加。`tests/abi/tuple.c` + `tests/abi/tuple_plain.c` 用元组和
  等价 struct 分别编译两个 TU，链接后互相调用；
- **布局不受 `#pragma pack` 影响**：`off = (off + align - 1) & -align` 在类型
  构造时一次算完并缓存，永不读 `pack_stack_ptr`。这样同一个元组在任何 TU、
  任何 pragma 状态下都是同一个 ABI（切片 `T[:]` 用的是同一条思路）；
- **类型回放写 `(T1, T2)` 而不是 tag 名**：`async`/`go` 的跳板要把形参类型在
  另一个合成 token 流里重新声明一遍。回放 tag 名会在当前作用域造出一个
  `sym_scope = 1` 的新 struct（和元组不兼容）；回放 `(T1, T2)` 则命中
  `c6_tuple_get` 的规范化缓存，拿回同一个类型；
- **`!error` 允许聚合返回值**：失败与否只看错误槽（`__tcc_err_flag_peek`），
  返回值只承载成功路径的值，而 C15 又强制它必须被 `try`/`catch` 消费——失败
  路径上那个值永远不可观测，所以 struct/union/元组直接清零即可，原来那条
  “必须能用 `-1` 表示哨兵”的限制只是实现偷懒。`float`/`double` 仍然不行
  （既不是标量哨兵也不是聚合），报 C58；
- **`_` 占位符沿用 `foreach` 的约定**：`_` 在解构模式里不建符号、不生成加载，
  别处仍是普通标识符（`int _ = 5;`、`(int, int) _ = f();` 照常）。全是 `_`
  报 C60——那等于把整个值丢掉，应该直接写 `f();`；
- **解构只做语句形式**：`(int q, int r) = f();` / `(q, r) = f();`，不做
  `:=` 类型推断、不做嵌套解构、不做 `t = {1, 2}` 这类字面量糖（要构造临时值
  就写 `(int, int){1, 2}`）。实现上把元组溢到隐藏局部 `__c6_tuple_tmpN`，
  再按普通赋值逐元素展开，所以目标可以是任意左值（变量、数组元素、成员）。

测试全部放在仓库根的 `tests/` 下，不进入上游的 `tests/tests2/`：c6 的
测试一律用 c6 自己的 `test` 块写（`tests/tests2` 里的 `.expect` 方案要
比对 stdout，改一行输出就得重生成期望文件）。

| 位置 | 内容 |
|---|---|
| `tests/async.c` | `async`/`await` 的返回值、挂起后局部变量、嵌套/循环里的 `await`、`defer` 跨挂起、`await c6_io`、任务槽回收；`co`/`task` 的句柄、自动回收、首步执行、轮转交替、`co` 在 `async` 体里仍是顶层、`__c6_co_run` 的错误计数 |
| `tests/thread.c` | `go` 起线程、参数按值拷贝、线程里的 defer/错误槽、线程里再 `go`、与主线程并行、`__c6_th_join_all`、软关键字当变量名/label/typedef 用、跨 TU 的 `go`（`tests/tu/thread.c`） |
| `tests/errors/spawn.c` | `go`/`co` 的错误路径：函数外使用、`go` 一个 `async` 函数、变参、`!error`、函数指针、不支持的签名、`co` 一个普通函数 |
| `tests/defer.c` | defer 的时机、顺序、`break`/`continue`/`goto`/`return`、递归 |
| `tests/err.c` | `err` / `errdefer` 与清理顺序、错误槽 |
| `tests/try.c` | `try` / `catch`，含 `goto` 离开、嵌套传播、`void!error` |
| `tests/test-block.c` | `test` 块、`-test`、`-test-filter` |
| `tests/macro.c` | `#macro` 与卫生的 `__local` |
| `tests/package.c` | `package` / `import` / `pkg.member`、多级包名、跨 TU |
| `tests/method.c` | 结构体方法、不同类型的同名方法、libc 方法（`p->fflush()`）、与方法或 libc 同名的普通函数不被顶掉，以及 `T!error` 方法 |
| `tests/interface.c` | 接口、vtable、隐式/显式转换、跨 TU |
| `tests/slice.c` | `T[:]` 的构造、下标、区间、跨 TU |
| `tests/typename.c` | 裸 tag 类型名：声明/成员/形参/返回值/方法接收者/切片元素/强转/`sizeof`/复合字面量/typedef/`const`、自引用链表、`typeof`/`_Generic`/`_Alignof`、接口方法签名、`TAG:` 标签、遮蔽（同名函数、同名局部变量、`struct stat` + `stat()`）、跨 TU（`tests/tu/typename.c`） |
| `tests/errors/typename.c` | 裸 tag 的退回路径：被函数/局部变量/POSIX `stat` 遮蔽、tag 未声明、tag 当值用、tag 出作用域、局部 tag 的切片、不完整类型 |
| `tests/foreach.c` | 数组/切片/区间三种可迭代对象、值绑定是副本、可迭代值只求值一次、`_` 丢弃、`break`/`continue`/`defer`/嵌套、绑定不泄漏、`size_t` 下标可写、`await` 跨挂起 |
| `tests/errors/foreach.c` | `foreach` 的全部编译期诊断（绑定数量、指针/整数/结构体/VLA/不完整数组、元素是数组、文件作用域、缺花括号） |
| `tests/tuple.c` | 元组的值语义、`t[k]`、`sizeof`/`_Alignof`、解构声明/赋值（变量、数组元素、成员）、`_` 占位符、元组当参数/成员/数组元素、嵌套与混合元素、复合字面量、跨 TU（`tests/tu/tuple.c`） |
| `tests/tuple_error.c` | 元组/struct 作 `!error` 返回类型：成功路径、失败路径、错误槽卫生、连续 `catch` |
| `tests/tuple_async.c` | 元组的 `async` 返回、`await` + 解构、`_` 丢弃、嵌套 `await` |
| `tests/tuple_thread.c` | 元组穿过 `go`（按值拷贝）、线程里的元组调用 |
| `tests/errors/tuple.c` | 元组的全部编译期诊断（元素类型、下标、解构形态、`return {…}`） |
| `tests/errors/loc.c` | 诊断位置：`sizeof(type)` / `_Alignof(type)` 整组回滚之后，头文件里的声明仍然报在头文件自己的那一行 |
| `tests/abi/tuple.c` + `tests/abi/tuple_plain.c` | 元组与等价 struct 的 ABI 互通（两个 TU 分别用 `-std=c6` 和纯 C 编译后链接） |
| `tests/abi/method.c` + `tests/abi/method_plain.c` | 方法改名的 ABI 互通：纯 C 一侧按原名调用 c6 方法（弱别名）、c6 一侧普通调用纯 C 函数 |
| `tests/tu/*.c` | 与同名主文件一起编译的第二个翻译单元 |
| `tests/errors/*.c` | 编译期诊断，见下 |

`tests/run-tests.sh`（`make selftest`）先对每个 `tests/*.c` 跑一次
`-test`，若存在同名 `tests/tu/<name>.c` 就一起编译成两个 TU；再对
`tests/errors/*.c` 的每个 `// EXPECT <片段>: <诊断>` 注释跑一次
`-D<片段>`，要求 stderr 含该诊断，并要求不带 `-D` 时该文件能编译运行通过。
诊断测试因此不需要维护行号。

`tests/abi/*.c` 是 ABI 回归：非 `*_plain.c` 的那个用 `-std=c6` 编译，
同名 `_plain.c` 用纯 C（不带 `-std=c6`）编译，两个 `.o` 链接后运行，
比对 `// EXIT <n>` 声明的退出码。这样“元组的 ABI 等于等价 struct”不是
注释里的承诺，而是每次 `make selftest` 都验一遍的事实。

`make test` / `make test-plain` 仍旧跑上游 `tests/tests2` 全量（134 项），
两种构建下都全过——c6 不再往 tests2 里加用例。

已知限制（与设计一致）：

- 运行时状态（defer 栈 / 错误槽 / 协程池 / 线程链表）是**每线程**一份，
  通过 `pthread_key`（不是 `__thread`：`-run` 不支持 TLS）取；`go` 出去的
  线程有自己的那一份，所以线程里照常写 `defer`/`err`/`co`，但跨线程共享
  数据仍需自己同步；
- `err` 一旦执行就直接返回，没有重抛形式；转发用 `try`（在返回 `T!error`
  的函数里自动传播），就地处理用 `catch`（快照并清空错误槽）；
- `T!error` 调用的返回值必须被 `try` 或 `catch` 消费，否则编译报错；
- `error` 只能作为返回值：形参表里的 `error` / `error*` / `error[]` 一律
  报错（`c6_check_error_param`），错误通道只有返回值 + 错误槽，
  不引入隐藏参数，也不改变调用约定；
- `!error` 的函数返回类型可以是标量、枚举、指针、struct、union、元组：标量在
  失败路径上返回哨兵 `-1`，聚合类型在失败路径上被清零（`c6_zero_local`）。
  因为 C15 强制返回值必须被 `try`/`catch` 消费，失败路径上的值不可观测，所以
  清零不丢信息。`float`/`double`/`long double` 仍不能标注（没有哨兵，也不是
  聚合）；
- 元组按自然对齐布局，`#pragma pack` 对它**无效**：这是有意的，跨 TU 的 ABI
  必须与 pragma 状态无关（和切片 `T[:]` 同一条规则）；
- 元组元素类型必须在**文件作用域**可见（内部 tag 注册在全局作用域），函数里
  定义的 struct 不能当元素；元素之间可能有 padding，所以不提供 `&t[0]` 式的
  连续访问，也不能把元组直接当 `T[:]` 的底层数组；
- 解构只支持**语句**形式，元素个数必须与元组一致；不做嵌套解构、`:=` 类型
  推断、`t = {1, 2}` 字面量糖、运行期下标 `t[i]`；`_` 只在解构模式里有特殊
  含义；
- `await` 一个失败的 `async` 子任务时，子任务的错误槽在恢复父任务帧时会被
  覆盖，父任务无法用 `catch` 观察子任务的错误：子任务要自己就地处理
  （`catch`），这是无栈协程“错误槽随帧保存/恢复”的已知限制；
- `async` 任务的帧上限 4096 字节、任务槽上限 1024 字节：元组返回值也占
  `val` 区，元素多的元组会更快撞到上限（撞到是运行时陷阱，不是语法错误）；
- 元组（和 struct）可以出现在 `interface` 的方法声明里，但结构体→接口的
  转换点只支持标量签名，聚合返回值会报 `'{' expected (got ';')`：这是接口
  特性自身的缺口，元组只是又撞上了它；
- `((int,int){1,2}[0])` 这种“复合字面量外面再套一层括号、下标写在括号里”的
  写法报 `')' expected (got '[')`：下标写在括号外（`((int,int){1,2})[0]`）或
  去掉外层括号即可；
- 元组/模式猜测会在失败时把扫过的 token 用 `begin_macro()` 塞回输入流，
  而猜测期间调用的 `parse_btype()`/`decl()` 自己也可能再塞一层回放；原来
  收尾的 `end_macro()` 会弹错栈顶，漏在外面的那层回放把 token 再发一遍
  （`((a) < b);` 这种“语句以括号组开头”的写法因此报
  `expression expected before '<'`）。现在这些点统一走 `tccpp.c` 的
  `macro_unwind_above()` / `end_macro_under()` / `macro_rewind()`：先把
  猜测期间压上去的回放全部弹掉，再回到自己的那一层；
- 结构体方法必须在调用点之前声明或定义（单趟编译约束）；只支持 `->`，
  `v.f()` 按普通成员访问处理，同名字段优先于方法；
- 方法符号是 `<结构体>_<方法>`，源码名是弱别名：同一个类型仍然只能有一个
  同名方法，不同类型的同名方法可以直接共存；`area(p)` 这种普通调用只指向
  本 TU 里第一个取到该名字的方法，有歧义时写 `p->area()`。`static` / `inline`、
  `__` 开头的名字、系统头文件里的声明、匿名结构体的方法和 `package` 内的
  方法保留原名，因此两个 `static` 或匿名结构体的同名方法仍然要靠包/改名区分；
  普通名已经被别的函数占用（同名普通函数、libc 声明）时不发别名，这样的方法
  只能写 `p->f()` 调用；
- 纯 C 翻译单元里定义的方法没有改名符号，c6 一侧只能用 `f(p)` 调用它，
  写 `p->f()` 会在链接时缺 `T_f`；
- `interface` 的一致性检查发生在第一次结构体→接口转换点，因此方法也要在转换点
  之前可见；文件作用域的接口初始化器不支持（没有运行时代码可生成），接口→接口
  是普通拷贝；
- `pkg.member` 必须先 `import`（或位于该包内）；包只是词法前缀，没有目录/文件语义，
  多级包名只是把点号换成 `__`；
- 变量初始化检查在**定义点**做（`c6_check_var_initialized`），不做数据流
  分析，因此 `int x; x = 1;` 也会报错；`extern` 声明、形参、结构体/联合体
  成员、`typedef` 不是定义，不受约束；
- VLA 是初始化规则的唯一豁免：标准 C 不允许 `int a[n] = …`，所以 VLA 仍可
  不带初始化器；
- 这条规则是**按翻译单元**生效的：不加 `-std=c6` 的旧代码零变化
  （plain 构建下 tests2 仍 134 项全过，c6 的自测在 `tests/` 单独跑）；
  给某个 TU 开启扩展后，该 TU 内所有定义都要补初始化器，K&R 形参声明表除外。TCC 会把显式 `= {0}` 的
  全局/`static` 对象放进 `.data`（不写初始化器时在 `.bss`），因此给大型
  静态缓冲区补初始化器会增大输出文件；
- TCC 自己的源码也按这条规则写：`make c6selfcheck` 用 `-std=c6 -DC6_EXT`
  编译 `tcc/tcc.c`（原生目标文件 + C99 后端产物；带 `C6_EXT` 才会连
  `#ifdef C6_EXT` 的扩展实现一起检查）并把编出来的编译器连起来跑，两步
  编译都不许有诊断输出。
  为此 `tcc/tcc.h` 里那批编译器全局量（`tok`、`file`、`int_type` 等）从
  "头文件里的暂定定义"改成"头文件 `extern` 声明 + 各自 `.c` 里带初始化器
  定义一次"（`ST_DATA` 的 `ONE_SOURCE` 分支也跟着去掉了 `static`）；
  c6 的关键字（`err`、`test`、`import` 等）在 tcc 源码里原来的标识符用法
  改成了别的名字；`x86_64-gen.c` 的 `gfunc_prolog` 定义补了 `ST_FUNC`，
  否则原型（`static`）和定义（`extern`）是两个符号，c6 的方法登记会报
  `duplicate method`；
- `-test` 与 `-run` 同时出现时 `-run` 不告警（两者都选择内存执行）；
- 切片 `T[:]` 是**视图**：`{元素指针, 长度}` 两个机器字，不拥有元素，也没有
  `append`/`cap`/动态扩容/深拷贝；`data` 字段是 `T *const`，所以切片头不能被
  重定向，越界检查以外的指针运算请走 `.data`；
- 切片元素的顶层 `const`/`volatile` 被忽略（`const int[:]` 与 `int[:]` 同类型），
  元素是 struct/union/enum 时其 tag 必须在文件作用域声明——函数内的局部 tag
  会随块退出释放，而切片类型要活到 TU 末尾；
- 数组→切片只在运行期上下文隐式发生（赋值/初始化/实参/`return`/强转）；
  静态初始化器（文件作用域或 `static` 局部）没有可执行代码，写
  `int[:] g = arr;` 会报错，改用 `{ arr, N }`。VLA 与不完整数组不能转切片，
  指针也不能直接切（用 `(T[:]){p, n}`）；
- 切片越界是运行时陷阱（SIGSEGV），没有诊断文本：libtcc1 里没有格式化
  输出，与 `catch` 落空时的处理保持一致；
- 元素本身是数组时先用 `typedef`（`typedef int Row[3]; Row[:] rows;`）——数组
  声明符只能写在名字上，C 本身如此。
- `async` 是**无栈**协程：函数体始终跑在调用方的栈上，`await` 处只保存
  已经用到的帧（`[base - size, base)`）、defer 记录和错误槽，`return` 回
  驱动方；恢复时原样拷回并跳到恢复点。因此没有独立栈、没有
  `setjmp`/`ucontext`，也没有上下文切换开销，但**不能**在 `await` 之后
  依赖别的协程帧还活着；
- 帧快照按 `-loc` 取“到目前为止用到的部分”，每次挂起都会更新
  `frame_size`；恢复只拷回最后一次挂起时记下的字节数，所以偏移必须
  在恢复后保持有效——TCC 的帧槽就是 `rbp + off`，天然满足；
- 协程池是**每线程**一个（`pthread_key`，不是 `__thread`：`-run` 不支持
  TLS）：64 个任务、每任务 4 KiB 帧、1 KiB 任务结构、256 条 parked defer
  记录。超出容量是运行时陷阱而不是诊断；
- 挂起点上已压在求值栈里的中间值必须落在**变量**里（帧槽会随帧一起
  恢复）。实践中调用点本来就会把活寄存器先落到帧槽，所以
  `plain() + await f()` 也能用；静态判断不出来时报约束 C33；
- `async` 的签名必须是能从声明写回任务结构的类型：标量、枚举、指针、
  `T[:]`、文件作用域 struct/union。数组（会退化成指针）、VLA、函数指针
  参数都会报 `'async' does not support this signature`；
- `__c6_co_destroy` 目前只回收任务槽（连同未完成的子任务一起），不会
  执行任务里还挂着的 `defer`；需要清理的协程请让它跑到完成；
- `await` 不能出现在 `defer`/`errdefer` 体、`sizeof` 等不求值上下文，
  也不能在非 `async` 函数里用；`c6_io` 只有 `await c6_io(fd, ev)` 一
  种写法；
- `co` / `go` / `task` 是**软关键字**：词法阶段仍是标识符，只在语句开头
  且后面紧跟一个标识符时才按关键字处理。所以 `int go = 1;`、`go = 2;`、
  `x = go;`、`go(x);`、`typedef int co; co v;`、`go:` 标签都照常工作；
  已经被 `typedef` 成类型名的 `co`/`go`/`task` 一定按声明解析；
- `co` 是**同步起点**：任务在 `co` 语句处就开始跑，跑到第一个必须等待的
  点才回到下一条语句。`await c6_io` 总要停一轮（唤醒是下一轮 `select()`
  的事，即使 fd 已经就绪）；`await` 子任务不算等待，所以不碰 fd 的
  `co f();` 会在 `co` 语句里跑完，并**当场回收**（没人拿得到句柄，也没人
  观察得到）；只有 park 在 fd 上的任务才留给 `__c6_co_run()`；
- `co`/`task` 的操作数必须是一次 `async` 调用；`go` 的操作数必须是一次
  **普通函数**调用（`go` 一个 `async` 函数会提示改用 `co`）。三种写法分工：
  `co` = 无栈协程、`task` = 拿句柄、`go` = OS 线程；
- `go f(a);` 的实参在 `go` 语句处求值一次并按值复制到堆上的参数块，之后
  改局部变量不影响线程；被调函数不能是 `async`、不能变参、不能带
  `!error`、不能走函数指针，形参不能是数组/VLA/函数指针——这几种都报
  约束 C38–C40，而不是悄悄退化成直接调用；
- 线程总是由**启动它的线程**回收（`main` 结束时编译器插
  `__c6_th_join_all()`，别的线程在退出时回收自己的子线程）；不提供取消，
  也没有分离线程。跨线程共享数据要自己加锁（和 C 一样）；
- `go` 只支持 ELF 目标；PE 目标上还没有线程支持，运行时会 trap；
- `c6_io` 的 fd 超出 `[0, 1024)` 时任务按失败回收（`__c6_co_wait` 返回
  `-1`，`__c6_co_run` 计一次失败），不会永远 park 在 select 里；
- 调度是**协作式**的：没有抢占，不 hook libc，阻塞的 `read`/`printf` 不会
  自动变成让出点；`__c6_co_run()` 对顶层任务做轮转，一次 `select()`
  等所有 park 住的 fd；
- 同时存活的协程上限是 64（含 `await` 出来的子任务），超了是运行时陷阱；
  `co` 出去但已在 `co` 语句里跑完的任务会立刻回收，所以循环里 `co` 短
  任务不会占满池子。
- 裸 tag 类型名只在**普通命名空间里没有同名符号**时生效（约束 C41）：
  `struct stat` 与 `stat()` 共存时，`stat(path, &st)` 仍是调用，`stat st;`
  必须写 `struct stat`。这条规则是刻意的——否则 `S = 3;`、`S * 2;`、
  `S(x);` 这些合法 C 会被 tag 抢走；
- 只接受**已经可见**的 tag，不会隐式前置声明：`S *p;` 在 `S` 未声明时
  仍报 `'S' undeclared`；定义处不能省略关键字（`Vec { int x, y; };` 不是
  语法，裸 `TAG { … }` 分不清 struct/union）；
- 语句开头的 `TAG(...)` 在 `TAG` 只是 tag 时按声明解析（与 C 里 typedef 名
  的规则一致），不再落到「隐式函数声明」；被函数遮蔽时不受影响。`TAG:`
  仍然是标签。
- 同一条规则带来两个边角：K&R 定义里形参名恰好与文件作用域 tag 同名时
  （`int f(S) int *S; { … }`），`TAG` 后跟标识符按原型解析而报
  `';' expected`，写 `struct S` 即可；块作用域的 `TAG;` 只是引用外层 tag，
  不会像 `struct TAG;` 那样声明一个新的局部不完整 tag。
- `foreach` 的可迭代对象只有三种：完整数组、切片 `T[:]`、整数区间
  `lo:hi`（半开）。不做运行期迭代协议（`has()`/`next()`）：`void *next()`
  会把元素类型擦掉，每轮都得装箱/分配；而方法名不按接收者改名，
  `has`/`len`/`at` 会和用户函数撞名。其它类型请暴露一个切片成员，再
  `foreach |i, v| obj.items`；
- 绑定表是 binder-first（`|i, v|` 在可迭代对象前面），循环体必须带
  花括号：省掉绑定、省掉花括号、两个绑定都写 `_` 都会报错而不是猜；
  点号区间 `0..count` 不做（词法上是两个浮点常量），统一写 `0:count`；
- 隐藏槽只对“求值一次”负责：数组每轮重新按 `data[i]` 取元素（数组本身
  可能被改），切片用求值时的 `{data, len}` 快照；值绑定是副本，写下标
  绑定等于改循环变量（与 `for` 一致）。

### 11.9 C99 输出后端（`-c99`）

新增 `tcc/c99-gen.c` + `tcc/c99-link.c`：复用全部前端（含 c6 语法），
把代码生成到 C99 文本而不是机器码。它不再是独立编译器，而是并进普通的
`tcc/tcc`：`tcc/Makefile` 把 `tcc.c` 以 `-DTCC_TARGET_C99` 单源再编一份
`tcc-c99.o`，再用
`objcopy --redefine-sym main=c99_main --keep-global-symbol=c99_main`
把其余全局符号本地化，然后链进 `tcc`；`tcc.c` 的 `wants_c99_output()` 在
解析命令行前决定是否转给 `c99_main()`：`-c99` 显式选择，或者 `-o` 以 `.c`
结尾 / 指向已存在目录（或以路径分隔符结尾）时自动选择；`-E`、`-shared`、
`-r`、`-run` 仍走原生路径。C 输出模式下 `-c` 表示“每个翻译单元一个 .c”，
`-test` 生成带测试入口的 C 源码；`-o <dir>` 按 TU 生成
`<dir>/<输入名>.c`，例如 `tcc -c -o out/ a.c b.c` 得到 `out/a.c`、
`out/b.c`。输出名会覆盖输入文件时直接报错，`-shared`/`-r`/`-run` 在 C 模式
下也直接报错，不会写出空的目标文件。驱动为每个 TU 重放一次自己时，
`c99-gen.c` 的静态生成器状态在 `tcc_new()` 里由 `c99_reset()` 清空，
否则第二个 TU 会继承上一个 TU 的函数体和声明。`c99-link.c` 里的兜底报错
发生在 `tcc_output_file()` 里，即 `tcc_compile()` 已经离开全局状态之后，
所以报错前要先 `tcc_enter_state()`，否则 `error1()` 会解引用空的
`tcc_state` 而崩溃。

（两个编译器各自持有静态状态，不能只按 `TCC_TARGET_*` 在运行时切换：
`tccgen.c` 的寄存器组、返回寄存器、参数求值顺序等都在编译期由 target 宏
决定，所以嵌入的是同一份源码按两个 target 编出的两份代码。）

寄存器 -> `__c99_r[8]` 联合体数组；栈帧 -> `__c99_frm.f` 字节数组（用 tcc
自己的局部变量偏移寻址，因此不需要给局部变量取名字）；比较 -> `__c99_cnd`；
数据段 -> 字节数组 + `__c99_init()` 按重定位表打补丁。

只被引用、不在本 TU 定义的外部符号（libc 函数，以及别的 TU 的函数/对象）
单独声明在 `__c99_ext_<n>` 下，再用 `__asm__` 标签绑回真正的链接符号，生成
代码通过 `#define` 用源名字引用它，这份 `#define` 在前导之前 `#undef` 掉。
原因是声明块里的原型只能来自 tcc 的类型模型（`long` 和 `long long` 是同一个
64 位整型，结构体一律是字节块），和 c6 运行时前导里 `#include` 的宿主头文件
（`<stdlib.h>`、`<pthread.h>`）对不上；换名字后两边永远不碰面，`gcc 1.c`
（不加 `-w`、不加任何额外开关）就能直接编译。本 TU 自己定义的函数（含
`<tag>_<name>` 方法别名）不算导入，仍按原名声明。`make c6selfcheck` 会把
c6 模式翻译出来的 `selfcheck.c` 交给宿主编译器编译一遍，钉住这条。

结构体/联合体、返回结构体的函数、间接调用都已经支持。c6 的运行时
（defer/errdefer、err/try/catch、切片、协程、线程、`c6_io`）用一份可移植
C99 + POSIX（pthread/select）的实现重写在 `tcc/c99-rt.inc`，构建时转成
字符串常量嵌入 `c99-gen.c`，每个 c6 输出文件自带这份运行时；在
GCC/Clang 下运行时符号是 weak 的，多个 TU 共用同一份状态，没有 weak 的
编译器退化为每个 TU 一份。`tests/*.c`、`tests/exit/*.c`、
`tests/abi/tuple.c` 和 `examples/*.c` 均已通过。

端到端回归直接复用 selftest 的整套用例：`BACKEND=c99 ./tests/run-tests.sh`
（即 `make c99test`）把 `tests/*.c`、`tests/errors/*.c`、`tests/exit/*.c`、
`tests/abi/*.c` 和 `tests/c99/*.c` 都先用同一个 `tcc/tcc`（`-o ...c`）生成
C 源码，再用
`gcc -std=c99 -pedantic -Wall -Wextra -Werror -pthread` 编译并运行，断言与
native 模式相同（退出码、`// OUTPUT` 输出、`EXPECT` 诊断；只在 C 输出后端
才不同的诊断用 `// EXPECT-C99` 声明）。`make selftest` 用同一个 `tcc/tcc`
跑同一套用例（`tests/c99/c99test.c` 用一份源码覆盖普通 C 和 c6 的全部特性）。

变参函数**定义**也支持：`tccdefs.h` 在 `__c99__` 下给出与 SysV x86-64
一致的 `__builtin_va_list`（`gp_offset`/`fp_offset`/`overflow_arg_area`/
`reg_save_area`），`__builtin_va_start`/`__builtin_va_arg` 复用
`x86_64-gen.c` 的 `classify_x86_64_va_arg`，而 C 后端把形参块 memcpy 进
`__c99_frm.f` 之后，再从 `frame_address(0) - 24` 那个位置把宿主编译器的
`va_list` 复制进变参函数的栈帧顶部——这正是 `__builtin_va_start` 会去读的
位置，所以生成代码里的 `va_arg` 与宿主 `vsnprintf` 之类拿到的 `va_list`
是同一份。整型/浮点变参、超过 6+8 个参数溢出到栈上的情形、
`tests/c99/c99test.c` 里的“把 va_list 转手给宿主函数”都已覆盖。

自举（`make selfhost` / `make c99selfhost`）：前者是 c6 后端编译自己
（`tcc/tcc` -> stage1 -> stage2，两次目标文件 `cmp` 相同），后者是 C 输出
路线（`tcc -c99` 出 C -> 宿主 gcc 编出 stage2 -> stage2 编出 stage3，stage3
再编一次目标文件与 stage2 的逐字节相同）。两条路线最后都让新编译器编一个
小程序并运行，因此“生成的后端能跑”是被回归卡住的，不是靠人工抽查。这两条
路线的存在也顺手钉死了几个只有在自举规模下才暴露的 bug：`nocode_wanted`
的语句不能混进活代码、LLONG_MIN 没有 C 字面量、`.bss` 段必须出现在输出里、
符号地址不能在生成时定死（tentative 声明之后定义还会搬位置）、移位的宽度
取**左**操作数的类型（`(unsigned long long)x << 32` 不能被截成 32 位移位）。

限制（均显式报错，不会静默错编译）：computed goto、VLA；非 POSIX 主机上
运行时可编译，但 `async`/`go`/`c6_io` 需要移植。
ABI 方面，不超过 8 字节且只含整数/指针成员的结构体按 x86-64 寄存器返回，
可以与普通 C 互操作；其它结构体返回用隐藏指针（c6 TU 之间一致，大于
16 字节时也与原生 ABI 一致，9--16 字节与普通 C 混用不支持）。
