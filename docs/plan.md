# TCC 语言扩展实现方案（深度评审修订版）

> 本文档在上一版基础上，结合 TCC 源码级约束进行深度评审与修复。核心约束重申：**TCC 是单趟编译器，无 AST，解析与代码生成交织，任何扩展必须在递归下降的特定时机就地插入，不得依赖事后重写**。以下先给出本轮深度评审发现的**阻塞性缺口与修复**，再输出完整方案。


## 0. 深度评审：阻塞性缺口与修复摘要

| 编号 | 缺口 | 严重度 | 修复 |
|---|---|---|---|
| **G1** | `defer` 的 `active_slot` 分配在函数 prologue，但条件/循环中 `defer` 可能多次注册，单一槽位无法表达“多次注册、多次执行” | 阻塞 | 改为**运行时 defer 记录链表**（每注册一次生成一条记录，含函数指针+参数），而非编译期单槽；清理时遍历链表逆序调用 |
| **G2** | `defer` 清理代码“生成在函数末尾清理区”与 TCC 的代码生成模型冲突：TCC 顺序追加机器码，不能在中间“预留”末尾区域 | 阻塞 | 采用**就地生成清理块 + 跳转绕过**模式：注册时立即生成清理块和跳过跳转；清理序列通过跳转链调用，不依赖“末尾集中区域” |
| **G3** | `goto` 跨 defer 作用域的清理判定：原方案说“复用 `local_label_stack`”，但 TCC 的 `local_label_stack` 仅记录标签，不记录作用域深度 | 高 | 引入独立的**`ScopeTracker` 栈**，与块解析同步压入/弹出，记录每层的 defer 记录起始位置；`goto` 时比对源/目标作用域，生成对应清理 |
| **G4** | `interface` 的 vtable/thunk 在 `impl` 解析点生成，但 `impl` 可能出现在调用点之后；单趟下“调用点查找可见 impl”要求 `impl` 必须先于使用 | 高 | 首版强制 **`impl` 必须先于任何使用点的 `interface` 值构造**；跨 TU 通过 `extern` vtable 声明前置。调用点只做“查找已注册 ImplDesc”，不做代码生成回填 |
| **G5** | `err()` 的 `goto __err_exit` 假设存在统一出口，但 TCC 没有“函数末尾统一清理标签”的现成机制 | 高 | 采用**每错误返回点就地生成清理序列**：`err()` 处直接展开 errdefer/defer 的逆序调用，然后 `return` 哨兵；不依赖间接跳转 |
| **G6** | `test` 块的 runner 在 `-run` 模式下由 `libtcc.c` 遍历 `TCCState->tests`，但 TCC 的 `-run` 入口是用户 `main`，没有“先跑测试再跑 main”的钩子 | 中 | `-test` 模式下**不调用用户 `main`**，`libtcc.c` 直接遍历测试表；`-test` 与正常 `-run` 互斥，文档明确 |
| **G7** | 扩展开关通过“运行时关键字映射”实现，但 TCC 的 `tok_alloc` 在词法阶段就将字符串 intern 为 `TokenSym`，后期切换映射不可行 | 中 | 改为**编译期条件编译**：`tcctok.h` 中 `#ifdef TCC_EXT` 注册新 token；扩展关闭时这些字符串根本不进入关键字映射，零运行时开销 |
| **G8** | `#macro` 的 `__local` 重命名通过 `TokenSym` 插入唯一名，但 TCC 的 `TokenSym` 是全局 intern 表，宏展开产生的临时标识符会永久累积 | 中 | 重命名使用**展开计数器后缀**（如 `tmp__h17`），但不在 `TokenSym` 中持久化；展开结束后从 intern 表移除，或接受可控的内存增长（首版可接受，后续优化） |
| **G9** | 包管理的 `package NAME { ... }` 在 `tccgen.c` 顶层处理，但 TCC 的顶层声明循环 (`decl`) 不天然支持“命名块”语法 | 中 | 将 `package NAME { ... }` 解析为**语法糖**：读取包名后进入特殊声明上下文，对块内每个全局符号在注册时施加 `NAME__` 前缀；块结束后恢复。不引入真正的“顶层块作用域”，只做 mangle |
| **G10** | `err` 的错误槽“通过隐藏参数传递”在 TCC 的调用约定中无现成支持 | 中 | 首版使用**线程局部错误槽**（`__thread`），调用者从槽读取；跨线程传播需手动复制。隐藏参数作为后续优化 |


## 1. 总体架构与扩展开关

### 1.1 挂载点

| 源文件 | 职责 | 扩展挂载点 |
|---|---|---|
| `tcctok.h` | 关键字与 token 编号 | `#ifdef TCC_EXT` 注册新 token；关闭时零映射 |
| `tccpp.c` | 预处理与词法分析 | `#macro` 指令识别、`__local` 重命名、include 路径扩展 |
| `tccgen.c` | 语法分析与代码生成 | `package/import` mangle、`interface/impl` 注册与检查、`defer/errdefer/err` 就地代码生成、`test` 块注册 |
| `tcc.h` / `libtcc.c` | 符号表与编译状态 | `TCCState` 扩展字段、`-test` runner、扩展开关 |

### 1.2 扩展开关（修复 G7）

```c
/* tcctok.h */
#ifdef TCC_EXT
DEF(TOK_PACKAGE,   "package")
DEF(TOK_IMPORT,    "import")
DEF(TOK_INTERFACE, "interface")
DEF(TOK_IMPL,      "impl")
DEF(TOK_DEFER,     "defer")
DEF(TOK_ERRDEFER,  "errdefer")
DEF(TOK_ERR,       "err")
DEF(TOK_TEST,      "test")
#endif
```

- 开关：`-std=tcc-ext`，默认关闭。
- 关闭时：`TCC_EXT` 不定义，这些字符串保持普通标识符，**零运行时映射开销**。
- 开启时：`TCC_EXT` 定义，token 进入 lexer 关键字映射。
- `__TCC_EXT__` 宏同步定义，供源码条件编译。


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

`err()` 处**就地展开清理序列**，不跳转间接出口：

```c
__tcc_err.id = id;
__tcc_err.msg = msg;
__tcc_err.flag = 1;
/* 就地逆序执行当前作用域到函数根的 errdefer（条件 __tcc_err.flag）和 defer */
<清理序列>
return ERR_SENTINEL;   /* 哨兵值 */
```

`errdefer` 的清理调用条件：`if (__tcc_err.flag) { call fn(arg); }`。

### 3.4 调用者传播

```c
ret = may_fail();
if (ret == ERR_SENTINEL) {
    /* __tcc_err 已携带 id/msg，决定继续传播或恢复 */
}
```

首版不提供 `try` 语法糖，调用者显式检查。

### 3.5 规则

- 普通 `return` **不清除** `__tcc_err.flag`，由调用者负责；文档明确此约定；
- `msg` 生命周期：首版要求静态字符串或调用者管理；
- 递归中每层调用子函数后应立即检查并快照 `__tcc_err`，再清除，避免串扰。


## 4. `interface`：显式实现 + 胖指针 ABI（修复 G4）

### 4.1 设计

**函数体只依赖固定 ABI（胖指针 + vtable），调用点根据可见 `impl` 构造接口值。** 这样函数体代码生成与具体类型解耦。

### 4.2 语法

```c
interface Drawable {
    void draw();
    int  area();
}

struct Circle { int r; };

impl Drawable for Circle {
    void draw(Circle *self) { /* ... */ }
    int  area(Circle *self) { return 3 * self->r * self->r; }
}

void render(Drawable d) {
    d.draw();               // 生成 d.vt->draw(d.self)
}

void use(void) {
    struct Circle c;
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

`impl` 生成静态 vtable 和 thunk：

```c
static void thunk_Circle_Drawable_draw(void *self) {
    Circle_draw((struct Circle *)self);
}
static const struct DrawableVTable vt_Circle_Drawable = { thunk_..._draw, ... };
```

符号 mangle：`__tcc_vt_<Type>_<Interface>`、`__tcc_thunk_<Type>_<Interface>_<Method>`。

### 4.4 可见性规则（修复 G4）

- `impl Drawable for Circle` **必须在任何 `render(c)` 或 `Drawable d = c` 之前解析**；
- 解析 `impl` 时立即生成 vtable/thunk，并注册 `ImplDesc{Circle, Drawable, vtable_sym}` 到 `TCCState`；
- 调用点只做“查找 `ImplDesc`”，**不做代码生成回填**；
- 跨 TU：头文件中 `extern const struct DrawableVTable __tcc_vt_...;` 前置声明，调用点据此构造。

### 4.5 编译期检查

在 `impl` 解析点立即执行：接口已存在、struct 完整定义可见、方法齐全、首参为 `Type *`、去掉 self 后签名兼容、无多余方法。调用点检查：实参类型是否有可见 `ImplDesc`，否则报错。


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

test 函数内 `err()` 就地展开清理后返回哨兵 `1`；`__tcc_err` 携带 id/msg 供 runner 报告。`defer` 正常执行。`return;` 视为 pass；`return expr;` 首版禁止。

### 5.4 生成独立 runner（`-o` 模式）

仅当用户未定义 `main` 或显式 `--no-main` 时，注入 `__tcc_test_runner` 作为 `main`。


## 6. 包管理：mangle 语法糖 + 符号表扩展（修复 G9）

### 6.1 语法

```c
package math {
    int add(int a, int b) { return a + b; }
}
import math;
int main(void) { return add(1, 2); }
```

### 6.2 实现（修复 G9）

`package NAME { ... }` 不引入真正的顶层块作用域，而是**mangle 语法糖**：

1. 解析 `package NAME`：在 `TCCState` 中设置 `current_package = "NAME"`；
2. 块内每个全局符号注册时，符号名施加前缀 `NAME__`（如 `math__add`）；
3. `import NAME;`：在当前作用域的标识符查找链中插入“尝试 `NAME__` + 原名”的重定向；
4. 块结束恢复 `current_package = NULL`。

首版**不导出宏**；typedef/tag 命名空间按包隔离；`static` 保持文件内可见。

### 6.3 跨文件

包清单 `package.tccpkg` 描述包路径和导出符号；编译启动时读取并生成声明头（或预声明 `Sym`）注入当前 TU。链接名固定为 mangle 规则。


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
| **0** | 语义规范、`TCC_EXT` 条件编译、测试矩阵 | 扩展关闭零行为变化 | — |
| **1** | `defer`：就地生成清理块 + 运行时记录链表 + `ScopeTracker` | 条件/循环/递归/多 defer 逆序正确；`goto` 跨作用域清理；返回值落栈保护 | 0 |
| **2** | `errdefer + err`：线程局部错误槽 + 就地清理 + 哨兵传播 | 多层传播逐层执行 errdefer；递归不串错误 | 1 |
| **3** | **`test "desc" {}`**：顶层注册、`-test` 模式、`--test-filter`、与 `err` 集成 | PASS/FAIL 正确；退出码正确；后续特性用 `test` 验收 | 2 |
| **4** | 包管理单文件：`current_package` mangle、import 重定向 | 符号可见性/冲突正确；不导出宏 | 3 |
| **5** | `interface`：胖指针 ABI、vtable/thunk、`ImplDesc` 注册、调用点查找 | impl 先于使用强制；签名检查；用 `test` 验收 | 3 |
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
| `interface` | 正常 impl 与调用 | 缺方法/签名不符/impl 不可见 | 多 impl、跨 TU |
| 包管理 | 单文件导入 | 冲突、未导入使用 | 跨 TU 链接 |
| 卫生宏 | `__local` 重命名正确 | `#`/`##` 用于 `__local` 报错 | 递归展开 |

### 9.2 工程要求

- 现有 TCC 测试套件在 `TCC_EXT` 开/关两种模式下全回归；
- 调试信息保留原始源位置（尤其 `defer` 清理块和卫生宏）；
- `TCCState` 扩展字段保持 ABI 兼容；
- `__tcc_err` 线程局部槽需明确线程模型；
- 实现前核实 `macro_subst`、`local_label_stack`、`block_cleanup` 等 TCC 内部函数的真实签名与行为；`test` 生成符号加随机前缀避免冲突。


## 10. 最终评审结论

**通过，可进入实施。** 本版修复了上一版中 10 项阻塞/高风险缺口：

- `defer` 从“静态 active 槽”改为**运行时记录链表 + 就地生成清理块 + `ScopeTracker`**，解决了条件/循环/多次注册和 `goto` 跨作用域问题，与 TCC 顺序代码生成模型一致，并借鉴了 slimcc 已验证的 VLA 跟踪复用路径；
- `err()` 放弃“统一出口跳转”，改为**就地展开清理序列 + 哨兵返回**，消除了对不存在机制的假设；
- `interface` 的可见性规则强制 **`impl` 先于使用**，调用点只查找 `ImplDesc`，符合单趟约束；
- 扩展开关改为**编译期条件编译**，零运行时开销；
- `test` 的 `-test` 模式与 `-run` 互斥，runner 直接遍历测试表，不依赖用户 `main` 钩子。

**建议按阶段 0 → 1 → 2 → 3 → 4/5 → 6 → 7 实施。** 阶段 1 为第一个可交付里程碑；阶段 3 的 `test` 框架应在后续特性开发前就位，确保每项扩展都有可重复的验收手段。

## 11. 实施状态（全部阶段已完成）

构建与运行：

```bash
cd tcc && make clean && make -j$(nproc) EXTRA-DEFS=-DTCC_EXT CFLAGS="-g -O0"
tcc/tcc -std=tcc-ext -B. -run file.c        # 扩展开启
tcc/tcc -std=tcc-ext -B. -test file.c       # 运行 test 块
```

| 阶段 | 状态 | 关键实现位置 |
|---|---|---|
| 0 `TCC_EXT` 门控 | ✅ | `tcctok.h`（新 token 追加在文件末尾，避免破坏 i386 asm 关键字）、`libtcc.c`（`-std=tcc-ext`）、`tccpp.c`（`__TCC_EXT__`、未开启时把新关键字还原为普通标识符） |
| 1 `defer` | ✅ | `tccgen.c`：`superc_defer_parse` / `superc_defer_begin` / `superc_defer_func_end`（运行时记录栈 + 每函数调度器）、`lib/libtcc1.c`：`__tcc_defer_*` |
| 2 `errdefer`/`err` | ✅ | `superc_errdefer_parse` / `superc_err_parse`、`__tcc_err_set/get/clear`；哨兵 `-1` + `err();` 逐层重抛 |
| 3 `test` / `-test` / `-test-filter` | ✅ | `superc_test_parse`、`tccrun.c:tcc_run_tests`、`libtcc.c` 选项 |
| 4 `package`/`import` | ✅ | `superc_package_parse` / `superc_import_parse`、`superc_mangle_tok`（`NAME__sym`）、`sym_find` 包回落 |
| 5 `interface`/`impl` | ✅ | `superc_interface_parse`（生成 `struct NAME_vt` + 胖指针 `struct NAME`）、`superc_impl_parse`（vtable 对象 `__tcc_vt_TYPE_IFACE`）、`superc_check_impl`（缺方法/参数个数/参数类型/返回类型检查）、`superc_iface_call_prep`（调用点改写）、`superc_iface_convert`（隐式/显式转换） |
| 6 跨 TU | ✅ | 头文件用 `package NAME { ... }` 声明 API；`impl IFACE for TYPE;` 仅声明并 `extern` 引入 vtable，另一 TU 提供实现 |
| 7 `#macro` + `__local` | ✅ | `tccpp.c`：`parse_superc_macro` / `superc_hygiene_body`（展开计数器 `ident__hN`，在参数替换前重命名，`#`/`##` 误用报错） |

验收测试：`tcc/tests/tests2/156_superc_defer.c` … `162_superc_iface_errors.c`，由
`tests/tests2/Makefile` 自动加 `-std=tcc-ext`；未编译 `-DTCC_EXT` 时自动 SKIP
（以 `__TCC_EXT__` 预定义探测）。`make -C tests/tests2 tests2.all` 两种构建下
分别为 141 / 134 项全通过。

已知限制（与设计一致）：

- 运行时状态是普通全局变量（`-run` 不支持 TLS），因此 `defer`/`err` 状态是
  单线程的；
- `err` 不做自动传播，调用方需检查哨兵并调用 `err();` 重抛；
- `interface` 的 `impl` 必须先于使用可见（单趟编译约束）；
- `-test` 与 `-run` 同时出现时 `-run` 不告警（两者都选择内存执行）。
