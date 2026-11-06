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
| **G10** | `err` 的错误槽“通过隐藏参数传递”在 TCC 的调用约定中无现成支持 | 中 | 首版使用**线程局部错误槽**（`__thread`），调用者从槽读取；跨线程传播需手动复制。隐藏参数作为后续优化 |


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
/* 线程局部错误槽（首版） */
__thread struct {
    int  flag;
    int  id;
    const char *msg;
} __tcc_err;
```

每个函数帧 prologue **不需要**独立错误槽；错误槽是线程局部的，但函数在**错误返回点**显式读取并决定传播。递归场景下，每层在调用子函数后检查 `__tcc_err.flag`，需要时复制到本地变量后清除，避免串扰。

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

/* 这两个函数就是实现：不改名、不改调用约定 */
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
调用普通 C 函数。`import` 只影响源码里的 `.` 写法，不产生也不引入任何文件/目录语义。

### 6.4 结构体方法

`ret f(struct T *self, ...)` 在文件作用域注册为 `T` 的方法，符号名不变；
`p->f(a)` 被改写成 `f(p, a)`（接收者先存临时槽，返回结构体时排在 sret 缓冲之后）。
字段优先，方法须先于调用点声明，`duplicate method` 检查按 (类型, 源码名) 做，
因此同名方法服务不同类型时要放进不同包。方法表与包 mangle 正交：包内定义的方法
仍然用源码里写的名字注册，所以 `import` 后可直接 `p->f()`。


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
| 结构体方法 | `p->f()`、方法调方法、sret、`T!error` 方法 | 不带调用、重复方法、只有 `->` | 跨 TU、与 `impl` 无关的普通函数调用 |
| 卫生宏 | `__local` 重命名正确 | `#`/`##` 用于 `__local` 报错 | 递归展开 |
| 变量初始化 | 各类定义带初始化器（标量/数组/结构体/`static`/`for` 声明） | 自动变量、全局、`static` 局部/全局缺初始化器报错 | VLA 豁免、`extern` 声明/形参/成员/`typedef` 不受限、`package` 内报原名 |

### 9.2 工程要求

- 现有 TCC 测试套件在 `C6_EXT` 开/关两种模式下全回归；
- 调试信息保留原始源位置（尤其 `defer` 清理块和卫生宏）；
- `TCCState` 扩展字段保持 ABI 兼容；
- `__tcc_err` 线程局部槽需明确线程模型；
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
| 4b 结构体方法 | ✅ | `decl()` 里 `c6_method_register`、`c6_method_lookup` / `c6_has_field` / `c6_method_call_prep`、`unary()` 的 `->` 分支、`c6_self_*` 隐藏接收者（sret 之后） |
| 5 `interface` | ✅ | `c6_interface_parse`（生成 `struct NAME_vt` + 胖指针 `struct NAME`）、`c6_auto_impl_ensure` / `c6_check_method`（惰性校验）、`c6_emit_auto_impls`（TU 末尾生成 `static` vtable）、`c6_iface_call_prep`（调用点改写）、`c6_iface_convert`（隐式/显式转换） |
| 6 跨 TU | ✅ | 头文件用 `package NAME { ... }` 声明 API；接口与方法声明放头文件，每个 TU 各自生成 vtable；`c6_tu_reset` 在每个 TU 开头丢弃上一 TU 的包/接口/方法/待生成 vtable |
| 7 `#macro` + `__local` | ✅ | `tccpp.c`：`parse_c6_macro` / `c6_hygiene_body`（展开计数器 `ident__hN`，在参数替换前重命名，`#`/`##` 误用报错） |
| 10 变量必须初始化 | ✅ | `tccgen.c`：`c6_check_var_initialized`，在 `decl()` 的对象定义分支（`has_init` 判定之后、TCC 把暂定全局改成 `extern` 之前）调用；`extern`/函数/`typedef`/`VLA`/alias 豁免；诊断使用 mangle 前的原始标识符 |
| 11 切片 `T[:]` | ✅ | `parse_btype()` 的 `case '['`（只认 `[:]`，其余 `unget_tok` 还原）→ `c6_slice_get`（按元素类型规范化生成内部 struct，固定布局、不走 `struct_layout`，因此 `#pragma pack` 无效）；`c6_slice_subscript`（`unary()` 的 `[` 分支，下标与区间）、`c6_slice_spill`（把基值落到本地 `{data, len}` 对）、`c6_slice_convert`（数组→切片：`vstore`/`gen_assign_cast`/强转/`decl_initializer`）；`type_to_str` 打印 `T[:]`；运行期 `libtcc1.c` 的 `__tcc_slice_index` / `__tcc_slice_sub`（越界空指针写陷阱） |
| 12 `async`/`await` 无栈协程 | ✅ | `parse_btype()` 的 `TOK_C6_ASYNC`（`FuncAttr.func_async`）→ `c6_async_declare`（用合成 token 在文件作用域声明 `struct NAME_task`）；`c6_async_definition` 把函数体按 `int __c6_body_NAME(void *task)` 编译，`c6_async_func_begin` 算帧基址并分发、`c6_async_params_copy` 把形参落成普通局部、`c6_async_func_end` 生成恢复分发器、`c6_async_finish` 写 `val` 并返回 `KIND_DONE`；`c6_async_call`（`unary()` 的 `tok_identifier` 分支）把调用改写成 `__c6_co_new` + 实参拷贝；`c6_await_parse`（`unary()` 前缀分支）+ `c6_async_suspend` 把帧/defer 记录/错误槽 park 出去；运行期 `libtcc1.c` 的 `__c6_co_new` / `__c6_co_await` / `__c6_co_suspend` / `__c6_co_restore` / `__c6_co_finish` / `__c6_co_wait` / `__c6_co_destroy`（固定池 + 手写 `select()`，不依赖 libc）；预置声明在 `tccpp.c` 的 `__TCC_ASYNC_DEFINED` 段 |

| 13 `go` / `task` 关键字 | ✅ | `tcctok.h` 里新增 `TOK_C6_TASK` / `TOK_C6_GO`（关键字，不能再当标识符用）；`c6_go_stmt`（`decl()` 与 `block()` 两个入口——单条语句不走 `decl()`）+ `__tcc_co_spawn`；`c6_task_decl` / `c6_task_check_handle`（`struct NAME_task` 的 `head` 必须是 `struct __c6_task`）/ `c6_task_defer_destroy`（复用 `__tcc_defer_push` 注册作用域清理）；运行期 `libtcc1.c` 的顶层就绪链表（`snext`/`busy`/`waiting`）与 `tcc_co_pump` / `tcc_co_step_once` / `tcc_co_link` / `tcc_co_unlink` / `__c6_co_run`，`__c6_co_wait` 也改成先推进顶层任务；`async` 体里的 `err` 走 `c6_async_propagate`（`KIND_ERR`），并禁止 `async` 方法是方法；预置声明同步加 `__tcc_co_spawn` / `__c6_co_run` |

测试全部放在仓库根的 `tests/` 下，不进入上游的 `tests/tests2/`：c6 的
测试一律用 c6 自己的 `test` 块写（`tests/tests2` 里的 `.expect` 方案要
比对 stdout，改一行输出就得重生成期望文件）。

| 位置 | 内容 |
|---|---|
| `tests/async.c` | `async`/`await` 的返回值、挂起后局部变量、嵌套/循环里的 `await`、`defer` 跨挂起、`await c6_io`、任务槽回收；`go`/`task` 的句柄、自动回收、首步执行、轮转交替、`go` 在 `async` 体里仍是顶层、`__c6_co_run` 的错误计数 |
| `tests/defer.c` | defer 的时机、顺序、`break`/`continue`/`goto`/`return`、递归 |
| `tests/err.c` | `err` / `errdefer` 与清理顺序、错误槽 |
| `tests/try.c` | `try` / `catch`，含 `goto` 离开、嵌套传播、`void!error` |
| `tests/test-block.c` | `test` 块、`-test`、`-test-filter` |
| `tests/macro.c` | `#macro` 与卫生的 `__local` |
| `tests/package.c` | `package` / `import` / `pkg.member`、多级包名、跨 TU |
| `tests/method.c` | 结构体方法与 `T!error` 方法 |
| `tests/interface.c` | 接口、vtable、隐式/显式转换、跨 TU |
| `tests/slice.c` | `T[:]` 的构造、下标、区间、跨 TU |
| `tests/tu/*.c` | 与同名主文件一起编译的第二个翻译单元 |
| `tests/errors/*.c` | 编译期诊断，见下 |

`tests/run-tests.sh`（`make selftest`）先对每个 `tests/*.c` 跑一次
`-test`，若存在同名 `tests/tu/<name>.c` 就一起编译成两个 TU；再对
`tests/errors/*.c` 的每个 `// EXPECT <片段>: <诊断>` 注释跑一次
`-D<片段>`，要求 stderr 含该诊断，并要求不带 `-D` 时该文件能编译运行通过。
诊断测试因此不需要维护行号。

`make test` / `make test-plain` 仍旧跑上游 `tests/tests2` 全量（134 项），
两种构建下都全过——c6 不再往 tests2 里加用例。

已知限制（与设计一致）：

- 运行时状态是普通全局变量（`-run` 不支持 TLS），因此 `defer`/`err` 状态是
  单线程的；
- `err` 一旦执行就直接返回，没有重抛形式；转发用 `try`（在返回 `T!error`
  的函数里自动传播），就地处理用 `catch`（快照并清空错误槽）；
- `T!error` 调用的返回值必须被 `try` 或 `catch` 消费，否则编译报错；
- `error` 只能作为返回值：形参表里的 `error` / `error*` / `error[]` 一律
  报错（`c6_check_error_param`），错误通道只有返回值 + 错误槽，
  不引入隐藏参数，也不改变调用约定；
- `!error` 的函数返回类型必须能表示哨兵 `-1`，因此结构体返回类型不能标注；
- 结构体方法必须在调用点之前声明或定义（单趟编译约束）；只支持 `->`，
  `v.f()` 按普通成员访问处理，同名字段优先于方法；
- 方法名共享 C 的全局命名空间：同一类型不能有两个同名方法，不同类型要用同名方法
  时必须放进不同 `package`；
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
- 切片越界是运行时陷阱（SIGSEGV），没有诊断文本：libtcc1 是 freestanding 的，
  与 `catch` 落空时的处理保持一致；
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
- 运行时是单线程固定池：64 个任务、每任务 4 KiB 帧、1 KiB 任务结构、
  256 条 parked defer 记录。`-run` 下没有 TLS，和既有的 defer/err 运行时
  一致；超出容量是运行时陷阱而不是诊断；
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
- `go` / `task` 是**真正的关键字**：`int go;`、`int task;`、`go:` 标签都不
  能再这么写。成员名不受影响（`s.go` 比较的是 token 值），但变量名、
  参数名、函数名、标签名都会报错——这是破坏性变更，写进 §2 的保留字表；
- `go` 是**同步起点**：任务在 `go` 语句处就开始跑，跑到第一个必须等待的
  点才回到下一条语句。`await c6_io` 总要停一轮（唤醒是下一轮 `select()`
  的事，即使 fd 已经就绪）；`await` 子任务不算等待，所以不碰 fd 的
  `go f();` 会在 `go` 语句里跑完，并**当场回收**（没人拿得到句柄，也没人
  观察得到）；只有 park 在 fd 上的任务才留给 `__c6_co_run()`；
- `go`/`task` 的操作数必须是一次 `async` 调用。普通函数的 `go f();` 留
  给未来的有栈协程：同一套首步约定下，只有显式让出原语（`c6_go_yield()`
  / `c6_go_wait_fd()`）才会让出，没有让出点就等价于直接调用。现在报
  `'go' on a non-async function needs stackful coroutines (not implemented
  yet)`；
- `c6_io` 的 fd 超出 `[0, 1024)` 时任务按失败回收（`__c6_co_wait` 返回
  `-1`，`__c6_co_run` 计一次失败），不会永远 park 在 select 里；
- 调度是**协作式**的：没有抢占，不 hook libc，阻塞的 `read`/`printf` 不会
  自动变成让出点；`__c6_co_run()` 对顶层任务做轮转，一次 `select()`
  等所有 park 住的 fd；
- 同时存活的协程上限是 64（含 `await` 出来的子任务），超了是运行时陷阱；
  `go` 出去但已在 `go` 语句里跑完的任务会立刻回收，所以循环里 `go` 短
  任务不会占满池子。

