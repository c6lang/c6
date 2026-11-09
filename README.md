# c6 —— 带语言扩展的 TinyCC

c6 是 [TinyCC](https://github.com/TinyCC/tinycc) 的一个分支，在不破坏原有 C 语义的前提下
加入了一组系统编程友好的语言扩展：

| 扩展 | 作用 |
|---|---|
| `defer { ... }` | 作用域/函数退出时逆序执行清理代码 |
| `errdefer { ... }` | 仅在错误路径上执行的 `defer` |
| `err(code, "msg")` | 设置错误槽、就地展开清理并直接返回 |
| `func() T!error` + `try` / `catch` | 声明可能失败的函数；`try` 原样传播，`catch e { ... }` 就地处理并清空错误槽 |
| `async` / `await` / `co` / `task` | 无栈协程：`co f();` 顶层跑、`task t = f();` 拿句柄 |
| `go f(...)` | OS 线程（`pthread_create`），语句立刻返回、真并发 |
| `test "描述" { ... }` + `-test` | 顶层测试块与测试运行器 |
| `package` / `import` | 包与名字修饰（`pkg__sym`），`pkg.member` 访问导出符号，支持多级包名 |
| 结构体方法 | `ret f(struct T *self, ...)` 即为 `T` 的方法，可用 `p->f(...)` 调用（符号是 `T_f`，不同类型可同名，`f(p)` 仍可用） |
| 省略 `struct` | tag 本身就是类型名：`Vec v;`、`sizeof(Vec)`、`Vec[:]`；被普通符号遮蔽时仍写 `struct` |
| `interface` | 胖指针（`{void *self; const VT *vt;}`）+ 自动生成的 vtable 接口抽象 |
| `T[:]` 切片 | 带长度与边界检查的数组视图（`{元素指针, 长度}` 两个机器字） |
| `foreach \|i, v\| a { ... }` | 遍历数组、切片与整数区间：绑定表在前，值绑定是元素副本 |
| `(T1, T2, ...)` 元组 | 函数多返回值：`(int, int) f(void) { return {q, r}; }`、`(int q, int r) = f();`、`(q, r) = f();`，元素用常量下标 `t[0]` 访问 |
| `#macro` + `__local` | 卫生宏：`__local` 标识符按展开序号重命名 |
| 变量初始化检查 | 每个变量定义都必须带初始化器（VLA 除外） |

扩展默认不启用，且是**编译期可裁剪**的：不定义 `C6_EXT` 构建出的编译器与上游 TCC 行为完全一致。

## 目录结构

```
.
├── docs/plan.md      设计与实施方案（含实现状态）
├── docs/grammar.md   扩展语法的 BNF/EBNF 规范
├── scripts/          bootstrap.sh：首次构建前跑 ./configure
├── tests/            c6 全部自测：*.c 是 test 块，errors/ 是编译期诊断
├── examples/         可运行示例（每个特性一个 *_demo.c）
├── Makefile          便捷入口（bootstrap / build / selftest / test / clean）
└── tcc/              TCC 源码树：上游基线 + 一个 c6 补丁一个提交
```

## 快速开始

依赖：`git`、`make`、C 编译器（gcc/clang）、`binutils`。

```sh
# 1. 配置（只有第一次需要；tcc/ 源码就在仓库里，不用联网拉取）
./scripts/bootstrap.sh          # 等价于 make bootstrap

# 2. 构建（打开扩展；会先自动执行 bootstrap）
make build                      # 等价于 make -C tcc -j EXTRA-DEFS=-DC6_EXT

# 3. 运行（-B tcc 让编译器找到未安装的 libtcc1.a 与运行时头文件）
./tcc/tcc -B tcc -std=c6 -run hello.c
```

`make build` / `make selftest` / `make test-all` 都会先跑 `bootstrap`：`tcc/`
已经配置过（有 `config.mak`）就直接跳过，没有才跑一次 `./configure`。

`tcc/` 是普通源码：上游 TinyCC 的基线提交 2d3eea2 先整体导入，之后每个 c6 补丁
一个提交（提交说明就是补丁标题）。要改 TCC 就直接改 `tcc/` 里的文件、按普通提交
提交——没有“重新同步工作树”这回事，手改不会被覆盖。`make distclean` 清理构建
产物和 configure 结果，不会删源码。

## 使用

扩展需要**编译器打开 `C6_EXT`** 并且**每个翻译单元加 `-std=c6`**（该开关同时定义
`__C6__`，方便写条件编译）。

### defer / errdefer / err

```c
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);

int load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        err(1, "open failed");
    defer fclose(f);                       /* 正常退出、return、break 都会执行 */

    char *buf = malloc(4096);
    defer free(buf);                       /* LIFO：free 先于 fclose */

    errdefer printf("load: cleaning up after error\n");

    if (fread(buf, 1, 4096, f) == 0)
        err(2, "read failed");             /* 返回 -1，errdefer 逆序执行 */
    return 0;
}

int main(void)
{
    int rc = load("data.bin");
    if (rc == -1) {                        /* 哨兵 -1 表示错误 */
        printf("error %d: %s\n", __tcc_err_id_get(), __tcc_err_msg_get());
        __tcc_err_clear();          /* 处理完错误，清空错误槽 */
    }
    return 0;
}
```

完整可运行示例见 [examples/err_demo.c](examples/err_demo.c)：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/err_demo.c
```

`err` 速查：

| 写法 | 行为 |
|---|---|
| `err(code, msg)` | 写入错误槽（`flag=1`），逆序执行本帧的 `errdefer` 与 `defer`，然后**直接返回**（返回值是哨兵 `-1`） |
| `defer stmt;` | 任何离开作用域的路径都执行（正常 `return`、`break`、`continue`、`goto`） |
| `errdefer stmt;` | 仅在错误路径（本帧执行了 `err`）执行，正常返回时跳过 |

配套的运行时接口（`libtcc1` 提供，直接在源码里声明即可）：

| 函数 | 用途 |
|---|---|
| `int __tcc_err_flag_get(void)` | 错误槽是否被设置 |
| `int __tcc_err_id_get(void)` | `err` 传入的错误码 |
| `const char *__tcc_err_msg_get(void)` | `err` 传入的消息指针 |
| `void __tcc_err_clear(void)` | 清空错误槽（调用方处理完错误后必须清） |

约定与限制：

- `err` 在**返回类型标注了 `!error` 的函数里**才能用，而且一旦执行就**直接返回**，
  同一函数里它后面的语句不会执行；
- `err` 没有“重抛”形式：要原样转发错误，用 `try`（见下一节），它会带着错误槽一起
  返回给调用者；
- `err` 会离开**整个函数帧**（循环/嵌套块里的 `defer` 都会执行），无法只退出内层块；
- `err` 不能出现在 `defer`/`errdefer` 体内（会编译报错）；
- 返回类型为 `void` 的函数里 `err` 直接返回，不产生哨兵值。

### `T!error` / `try` / `catch`

函数返回类型后面加 `!error` 表示“可能失败”。`err(id, msg)` 在这类函数里是
直接返回的简写；调用方可以用 `try` 原样传播，或用 `catch` 就地处理。

```c
/* 返回类型后面加 '!error'（也可以写在声明符后面：int parse(const char *)!error） */
static int!error parse(const char *s)
{
    if (!s)
        err(1, "string is NULL");         /* 设置错误槽并直接返回 */
    return s[0] - '0';
}

/* try：成功时原样给出返回值，失败时把错误原样交还给调用者 */
static int!error twice(const char *s)
{
    int v = (try parse(s));
    return v * 2;
}

/* catch：拿到 err(id, msg) 的值，并自动清空错误槽 */
int handle(const char *s)
{
    int v = twice(s) catch e {
        printf("caught %d: %s\n", e.id, e.msg);
        return -1;                        /* 处理块必须自己离开 */
    };
    return v;
}
```

速查：

| 写法 | 行为 |
|---|---|
| `int!error f(void)` / `int f(void)!error` | 声明可能失败的函数；两种位置等价 |
| `try f()` | 调用 + 检查错误槽；失败时执行本帧 `errdefer`/`defer` 并返回哨兵 `-1` |
| `try f();` | 语句形式，返回值被丢弃 |
| `e = f() catch { ... }` | 只在失败时进入处理块，不绑定错误值 |
| `f() catch e { ... }` / `f() catch (e) { ... }` | 处理块里可用 `e.id` / `e.msg` |
| `err(id, msg)` | 设置错误槽并立即返回；必须在 `T!error` 函数里使用 |

`e` 的类型就是 `error`（编译器预置的 `typedef struct __tcc_error { int id; const char *msg; } error;`）。
每次 `T!error` 调用之前都会先清空错误槽，`catch` 在进入处理块前再**快照并清空**一次，
所以连续两个 `catch` 不会串扰，也不会把之前遗留的错误误判成这次调用的失败。

约束（违反时编译报错）：

- `try` 只能出现在返回类型带 `!error` 的函数里，且操作数必须是一次调用；
- `catch` 的操作数也必须是一次调用，被调函数要返回 `T!error`；
- `catch` 处理块必须自己离开：`return` / `err(id, msg)` / `goto`；编译器判断
  不出来时会在处理块末尾插入一个陷阱，运行时崩溃而不是悄悄走成功路径；
- `try` / `catch` / `err` 不能写在 `defer` / `errdefer` 体内；
- `T!error` 函数的返回值**必须**被 `try` 或 `catch` 消费；直接赋值、参与运算、
  作条件、`return` 出去或者当语句丢弃都会编译报错；
- `error` 是**只出不进**的类型：可以作为返回值，但不能作为函数参数
  （`error`、`error*`、`error[]` 都不行）——错误只能从返回值出来，
  不走实参、也不走 out 参数；函数体内的 `error e;` 局部变量仍然可用；
- `!error` 的函数返回类型可以是标量、枚举、指针，也可以是 struct / union / 元组：
  失败与否只由**错误槽**决定，返回值只承载成功路径的值。标量在失败路径上
  返回哨兵 `-1`，聚合类型在失败路径上被清零——因为 `T!error` 的返回值只能被
  `try` / `catch` 消费，那个值永远不可观测，所以清零不会丢信息；
- `float` / `double` / `long double` 不能作 `!error` 的返回类型（没有可用的
  哨兵，也不属于聚合类型）。

完整可运行示例见 [examples/try_demo.c](examples/try_demo.c)：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/try_demo.c
```

语法规范（EBNF）见 [docs/grammar.md](docs/grammar.md)。

### test 块

```c
test "add works" {
    if (add(1, 2) != 3)
        err(1, "1 + 2 != 3");
}
```

```sh
./tcc/tcc -B tcc -std=c6 -test tests.c               # 运行全部 test 块
./tcc/tcc -B tcc -std=c6 -test -test-filter=add tests.c
```

运行器输出 `PASS:` / `FAIL:` 行与 `N test(s), M failure(s)`，全部通过时退出码为 0。

### package / import

```c
package math {
    int add(int a, int b) { return a + b; }
    int counter = 0;
}

import math;

int main(void)
{
    math.counter = 1;                 /* 包限定访问 */
    return math.add(1, 2) + math__add(1, 2);   /* 也叫 math__add */
}
```

规则：

- `package NAME { ... }` 里的符号被词法修饰成 `NAME__sym`（`main` 除外）；包只是
  名字前缀，**不引入目录或文件语义**；
- `import NAME;` 打开 `NAME.member` 这种写法，必须在**使用点之前**出现；包块内部
  引用自己的成员不需要 import。不 import 而写 `math.add` 会报
  `'math' is not an imported package (use 'import math;')`；
- 点号也可以写成多级：`package acme.util { ... }` / `import acme.util;`，
  前缀是 `acme__util`；嵌套块 `package acme { package util { ... } }` 与
  `package acme.util;`（文件级）都会组合前缀，结果相同；
- `pkg.member` 只解析到「包里的符号」这一层：`math.origin.x`、`*&math.origin.x`
  会把尾部 `.x` 交回普通成员访问，因此包里的全局结构体对象可以照常使用；
- 局部变量或字段与包同名时**字段优先**（普通 C 成员访问语义不受影响）；
- 未声明的包成员按普通未声明标识符报错，诊断使用源码里写的 `pkg.member` 而不是
  修饰后的名字。

跨 TU 时，头文件里用 `package NAME { ... }` 写出声明（或写 `extern`），实现放在
另一个 TU 的同一个包里即可；两边的符号名都是 `NAME__sym`，链接后就是普通 C 符号。
反过来，手写的老 C 代码只要声明 `int math__add(int, int);` 就能直接调用包的导出，
扩展 TU 也能直接调用普通的 C 函数。

### module / import 加载

`package` 只给一段代码加前缀；`module` 给**整个文件**加前缀，并且 `import`
会去找模块文件、加载它的声明：

```c
/* math.c */
module math;

struct Point { int x, y; };
typedef struct Point Point;
int add(int a, int b) { return a + b; }
int area(Point *p) { return p->x * p->y; }

/* main.c */
import math;

int main(void)
{
    Point p = { 3, 4 };
    return add(1, 2) == 3 && area(&p) == 12 ? 0 : 1;
}
```

- `module NAME;` 必须是文件的第一个声明；它把文件作用域的声明改成
  `NAME__sym`（点号 `module geo.circle;` 的前缀是 `geo__circle__`），块作用域、
  `static`、`test`、`__` 开头的名字和 `#include` 的普通 C 文件不受影响；
- 方法照旧按接收者取名：`int area(Point *p)` 导出 `math__Point_area`；模块文件
  **不**为方法发原名弱别名，C 侧只看到前缀符号；
- 类型也能写全限定名：`math.Point p = { 3, 4 };` 解析到 `math__Point`
  （模块里的 typedef 或 struct/union/enum tag 都行；多级 `tu.module.Point`
  同理；package 里的 tag 不加前缀，所以 package 限定类型要写 typedef）；
  只有当前 module/package 或 `import` 过的名字可以这样写；
- 模块里不能定义 `main`；
- `import NAME;` 先登记名字，再按「当前文件目录 → `-I` 路径 → 当前目录」查找
  `NAME.c`（点号形式 `a/b.c`），找到就把声明读进来：函数只取原型、跳过函数体，
  全局变量只取 `extern`、跳过初始化器；找不到文件时只登记名字（旧行为）；
  循环 import 报 `circular import of module 'x'`；
- 导入只带**声明**，代码仍在模块自己的目标文件里：链接时把 `math.o`（或库）放上
  命令行；
- `-emit-header FILE`（配合 `-c -std=c6`）可把模块导出写成普通 C 头文件：
  `tcc -B tcc -std=c6 -c -emit-header math.h math.c`。头文件里是
  `struct math__Point`、`math__add`、`math__Point_area` 这些真正的 C 名；
  切片和元组会生成 `math__slice_N` / `math__tuple_N` typedef。纯 C 一侧
  `#include "math.h"` 就能调用，不需要 `-std=c6`，也不需要知道模块的存在。

### 结构体方法

```c
struct Vec { int x, y; };

int vec_sum(struct Vec *self) { return self->x + self->y; }
int vec_twice(struct Vec *self) { return self->vec_sum() * 2; }

/* 不同类型可以同名：符号分别是 Circle_area / Rect_area */
struct Circle { int r; };
struct Rect { int w, h; };
int area(struct Circle *c) { return 3 * c->r * c->r; }
int area(struct Rect *r) { return r->w * r->h; }

int main(void)
{
    struct Vec v = {3, 4};
    struct Rect r = {2, 3};
    return (&v)->vec_twice() + (&r)->area();   /* 等价于 vec_twice(&v) + Rect_area(&r) */
}
```

- 文件作用域的函数声明/定义，只要**第一个参数是指向结构体（或联合体）的指针**，
  就注册为该类型的方法；调用 `p->f(a)` 被改写为 `T_f(p, a)`；
- 方法就是普通函数：**调用约定不变**，接收者就是第一个实参；符号名是
  `<结构体>_<方法>`，源码里写的名字另外发一个**弱别名**，所以
  `f(p, a)` 这种普通调用、手写 C、纯 C 翻译单元都照旧能链接；
- 不同类型用同名方法**不需要** `package`（`Circle_area` 与 `Rect_area` 各自独立）；
  弱别名只指向本 TU 里第一个取到该名字的方法，真有歧义时写 `p->area()`；
- 普通名已经被别的函数占着时（同名普通函数，或 `getpid` 这类 libc 头里的声明）
  就不再发别名：`p->getpid()` 照旧可用，普通调用 `getpid()` 仍然是原来的函数；
- 不改名的情形：`static` / `inline` 函数、`__` 开头的名字（编译器和 libtcc1 专用）、
  系统头文件里的声明（`p->fflush()`、`p->gettimeofday()` 这类 libc 成员照旧可用）、
  匿名结构体的方法，以及在 `package` 里声明的方法（包名已经区隔，符号是 `pkg__name`）；
- 只支持 `->`（`p->f()`）；`v.f()` 仍是普通成员访问，`p->f`（不带调用）会报
  `method 'f' must be called`；
- 结构体同名字段优先于方法；方法须在**使用点之前**可见（TCC 单趟编译）；
- `package` 里的方法名不受影响：`package circle { int area(Circle *self) ... }` 与
  `package rect { int area(Rect *self) ... }` 仍然可以共存，各自 `p->area()`；
- 返回结构体的方法也支持（隐藏的 `self` 排在 sret 缓冲之后）；
- `T!error` 方法照常工作：`int!error f(struct T *self)` 可用 `try p->f()` / `p->f() catch e { ... }`。

### 省略 struct / union / enum

tag 本身就是类型名，不用再写 `struct`（`union` / `enum` 同样）：

```c
struct Vec { int x, y; };
struct Node { int v; Node *next; };      /* 定义体内就能省 */
union Word { int i; float f; };
enum Color { RED = 1, GREEN = 2 };

Vec vec_add(Vec a, Vec b) { Vec r = { a.x + b.x, a.y + b.y }; return r; }

int main(void)
{
    Vec v = { 3, 4 };                    /* 变量定义 */
    Vec *p = &v;                         /* 语句开头的 'Vec *p' 是声明 */
    const Vec *cp = &v;                  /* 限定符照常 */
    Color c = (Color)GREEN;              /* 强转、sizeof(Vec)、复合字面量 (Vec){1,2} */
    Vec arr[2] = { { 1, 2 }, { 3, 4 } };
    Vec[:] vs = arr;                     /* 切片元素 */
    return (int)sizeof(Vec) + p->x + c;
}
```

- 规则只有一条：**普通命名空间优先**。名字被变量/函数/枚举常量占住时仍是老 C 语义，
  需要类型就写 `struct`。所以 `<sys/stat.h>` 里 `struct stat` 与函数 `stat()` 共存时，
  `struct stat st;` 与 `stat(path, &st)` 都照旧，`stat st;` 不行；
- 同名 `typedef` 也优先（`typedef struct Vec Vec;` 之后 `Vec` 按 typedef 解析，结果相同）；
  `interface` 名字本来就是这么生成的，行为不变；
- 只接受**已经可见**的 tag，不会隐式前置声明：`S *p;` 在 `S` 没声明时仍是 `'S' undeclared`；
- 语句开头的 `TAG:` 仍是标签；`TAG` 只是 tag 时 `TAG(...)` 按声明解析（与 C 里 typedef 名
  的规则一致），被函数遮蔽时按调用解析。同一条规则也意味着 K&R 定义里「形参名恰好与
  文件作用域 tag 同名」会按原型解析（`int f(S) int *S; {…}`），写 `struct S` 即可；
- 只在 `-std=c6` 的翻译单元里生效，`struct Vec v;` 永远可用。

完整可运行示例见 [examples/typename_demo.c](examples/typename_demo.c)：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/typename_demo.c
```

### 变量必须初始化

扩展模式下，**每个变量定义都必须带初始化器**，否则编译报错
（`variable 'x' must be initialized`）：

```c
int counter = 0;                  /* 文件作用域的定义也要写 */
static int instances = 0;

int main(void)
{
    int i = 0;
    int a[3] = {1, 2, 3};
    struct Circle c = {0};
    const char *p = 0;
    for (int k = 0; k < 3; k++)   /* C99 for 声明同样受限 */
        i += k;
    return i;
}
```

速查：

| 写法 | 行为 |
|---|---|
| `int x = 0;` / `int a[3] = {0};` / `struct S s = {0};` | 合法 |
| `int x;`（自动变量、全局、`static` 局部/全局、`for` 声明） | 报错 |
| `extern int x;` | 合法（声明，不是定义） |
| 函数形参、结构体/联合体成员、`typedef` | 合法（不受此规则约束） |
| `int a[n];`（VLA） | 合法：标准 C 不允许 VLA 带初始化器，故唯一豁免 |

规则是**定义点检查**，不做数据流分析：`int x; x = 1;` 一样报错。
诊断使用源码里书写的标识符，`package` 内的变量也报原名而不是 `pkg__x`。

旧代码怎么处理：

- **不开启扩展就完全不变**：不带 `-std=c6` 编译的翻译单元与上游 TCC
  逐字节一致，老项目（包括 TCC 自己的源码）不需要改任何一行；
- 扩展是**按翻译单元**开启的：一旦给某个 `.c` 加了 `-std=c6`，这个 TU
  里出现的所有定义都要满足规则（包括它包含的头文件里的函数体）；
  系统头文件实测可以正常通过（`stdio.h` / `stdlib.h` / `pthread.h` 等）；
- K&R 老式定义：**形参声明表不受影响**（它们是形参），函数体里的局部变量
  仍然要初始化；
- 常见迁移写法：

```c
int i;                    ->  int i = 0;
char buf[64];             ->  char buf[64] = {0};
struct S s;               ->  struct S s = {0};
int *p;                   ->  int *p = 0;
int g;                    ->  int g = 0;      /* 全局定义同样要写 */
for (int k; ...)          ->  for (int k = 0; ...)
int a[n];                 ->  不用改（VLA 豁免）
```

注意两个开销：

- 局部的大数组/大结构体加 `= {0}` 会真的清零（编译成一次 `memset`）；
  如果原来就靠后续 `read`/`memset` 填满，这次清零是新增开销；
- 在 TCC 里，**显式**写 `= {0}` 的全局/`static` 对象会落到 `.data`
  （文件里真的存一份零），而不带初始化器的全局对象在 `.bss`。所以给
  大缓冲区补初始化器会让可执行文件变大；迁移大型静态表时要留意这一点。

TCC 自己的源码也遵守这条规则（`make c6selfcheck` 会检查），因此拿
`-std=c6` 编 tcc 自己不会撞上这条诊断；带 `-DC6_EXT` 也一样，`#ifdef
C6_EXT` 里的扩展实现同样补了初始化器：

```sh
./tcc/tcc -B tcc -std=c6 -DC6_EXT -c tcc/tcc.c -o tcc.o   # 原生目标文件
./tcc/tcc -B tcc -std=c6 -DC6_EXT -o tcc.c.c tcc/tcc.c    # C99 后端的输出
```

为此 `tcc/tcc.h` 里那批编译器全局量（`tok`、`file`、`int_type`……）改成了
"头文件里 `extern` 声明 + 各自 `.c` 文件里带初始化器定义一次"，全局量
不再依赖"暂定定义"这种不带初始化器的写法。

### interface

```c
interface Shape {
    int area();
    const char *name();
}

struct Circle { int r; };

/* 结构体「拥有」接口声明的方法，就实现了这个接口；不再有 impl 语法 */
int area(struct Circle *self) { return 3 * self->r * self->r; }
const char *name(struct Circle *self) { return "circle"; }

int describe(Shape s) { return s.area(); }   /* 结构体值隐式转换为胖指针 */

int main(void)
{
    struct Circle c = {2};
    return describe(c) == 12 ? 0 : 1;
}
```

- 接口值是 `{void *self; const VT *vt;}` 胖指针，`s.area()` 通过 vtable 分派；
- **没有 `impl` 声明**：结构体是否实现接口，由「是否声明了接口要求的全部方法」
  决定；检查是惰性的——第一次发生结构体到接口的转换（函数实参、赋值、显式强转）
  时才比对方法集合与签名，缺方法、参数个数/类型、返回类型不一致都会编译报错，
  诊断指出具体的接口与方法；
- 转换点的前提是方法**在此之前**已经声明或定义；结构体到接口的转换在
  **文件作用域初始化器**里仍然不支持（没有运行时代码可生成），接口到接口是普通拷贝；
- vtable 是一个 `static const` 对象，每个 TU 对「用到的 (类型, 接口) 对」各生成一份，
  因此两个 TU 可以同时转换同一个类型而不会符号冲突；
- `impl` 现在是普通标识符，`int impl = 1;` 合法。

### 切片 `T[:]`

```c
int a[5] = {10, 20, 30, 40, 50};
int[:] s = a;                  /* 数组隐式转切片：len 取数组长度 */
int[:] t = a[1:4];             /* 区间 [lo:hi)：t.len == 3，t.data == &a[1] */
int x = s[0];                  /* 下标带边界检查 */
s[0] = 99;                     /* 写穿到数组 */
int n = (int)s.len;            /* 长度 */
char[:] m = "hello";           /* 字符串字面量也是切片（len 含结尾 NUL） */
```

切片是**视图**，不是容器：它不拥有元素、不扩容、不复制。值是
`struct { T *const data; const size_t len; }`，两个机器字，按值传递。

速查：

| 写法 | 行为 |
|---|---|
| `int[:] s = a;` | 数组 → 切片（数组必须是编译期长度，VLA/不完整数组不行） |
| `s[i]` | 边界检查后访问元素；`i` 先转 `size_t`，负数或越界都触发陷阱 |
| `s[lo:hi]` / `s[lo:]` / `s[:hi]` / `s[:]` | 子切片；要求 `lo <= hi <= len` |
| `a[1:3]` | 数组上的区间，上界是数组长度 |
| `s.data` / `s.len` | 元素指针（不做边界检查的转义出口）/ 长度 |
| `(T[:]){p, n}` | 用指针 + 长度手工构造切片（指针不能直接切片） |
| `sizeof(int[:])` | `2 * sizeof(void *)` |

规则：

- 切片可以出现在变量、函数参数/返回值、结构体成员、`typedef`、
  `sizeof`/`typeof`、复合字面量的位置；`int[:] *p` 是指向切片的指针，
  `int[:] a[3]` 是切片数组；
- 元素可以是任意**完整对象类型**（含指针、数组、结构体、嵌套切片）。
  元素是数组时先 `typedef`（`typedef int Row[3]; Row[:] rows = grid;`）——
  `[3]` 这种后缀声明符不能写在类型前面，C 本身也不允许；
- 切片的 struct/union/enum 元素必须在**文件作用域**声明（函数内局部的
  `struct` 会随块退出释放，而切片类型要活到 TU 末尾）；
- 切片元素的 `const`/`volatile` 限定符按**忽略**处理（`const int[:]` 与
  `int[:]` 同类型）：`data` 指针本身是 `const`，切片头不能被重定向；
- 越界是**运行时陷阱**（SIGSEGV），与 `catch` 落空时的处理一致：libtcc1
  里没有格式化输出，不产生诊断文本；
- 静态初始化器（文件作用域或 `static` 局部）没有运行时代码，所以
  `int[:] g = arr;` 不支持，写 `int[:] g = { arr, 5 };`（两个字段都是常量，可用）；
- 只是普通 C 语义的补充：`int a[3]`、`int a[] = {…}`、`void f(int[])`、
  `typedef int T[]` 全部保持原样，`int[]` 并**不**是切片写法。

完整可运行示例见 [examples/slice_demo.c](examples/slice_demo.c)：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/slice_demo.c
```

跨 TU 时两边各自写 `T[:]` 即可（布局固定），不需要额外声明。

### `foreach |i, v| a { ... }`

```c
int a[5] = {10, 20, 30, 40, 50};
int[:] s = a[1:4];

foreach |i, v| a { ... }        /* 数组：i 是 size_t 下标，v 是元素副本 */
foreach |i, v| s { ... }        /* 切片：同上，长度是求值那一刻的快照 */
foreach |i| 0:5 { ... }         /* 区间 [0, 5)：只要一个绑定 */
foreach |_, v| a { ... }        /* '_' 丢弃这个绑定 */
foreach |i, v| obj.items { ... }/* 结构体暴露的切片成员 */
```

速查：

| 写法 | 行为 |
|---|---|
| `foreach \|i, v\| 数组` | 两个绑定：`i` 是 `size_t` 下标，`v` 是元素副本 |
| `foreach \|i, v\| 切片` | 同上；可迭代值只求值一次（`{data, len}` 快照） |
| `foreach \|i\| lo:hi` | 半开区间 `[lo, hi)`；下标是两界的公共整数类型，`lo >= hi` 零次 |
| `foreach \|_, v\| …` / `foreach \|i, _\| …` | 丢弃下标 / 丢弃值（丢弃的绑定连加载都不生成） |
| `foreach \|_\| 0:n { … }` | “重复 n 次”的写法 |
| 值绑定 `v` | 副本：写它不改容器 |
| 下标绑定 `i` | 就是循环变量：写它改变后续迭代（同 `for`） |
| `break` / `continue` / `defer` / `await` | 与写在 `for` 体里完全一致 |

规则：

- 可迭代对象只有三种：**完整数组**（含字符串字面量）、**切片 `T[:]`**、
  **整数区间 `lo:hi`**。指针、整数、VLA、不完整数组、没有暴露切片的
  结构体都报错，并给出改法提示；
- 只求值一次：数组/切片落到隐藏的 `{元素指针, 长度}` 帧槽（与切片的
  运行期布局相同），区间上界落到隐藏整数槽，因此循环体里改容器不影响
  迭代次数；
- 循环体**必须**是花括号块，绑定的作用域就是这条语句；绑定表写作
  `|名字, 名字|`，`_` 表示丢弃；数组/切片要两个绑定、区间要一个；
- 元素本身是数组时（`int[2][3]`、`Row[:]`）不能遍历：值绑定会是一个
  数组，而数组在 C 里不可赋值；改用下标区间
  `foreach |i| 0:(int)rows.len { … rows[i] … }`；
- 下标/值都是普通局部变量，所以可以在 `async` 函数体里用（隐藏槽随帧
  一起保存/恢复），也可以在 `defer` 体、嵌套循环里用；
- `foreach` 仍然可以当普通标识符：`int foreach = 1;`、`foreach(x);`、
  `p->foreach`、`goto foreach;` 都照常工作——只有语句开头、且下一个记号
  是 `|` 时才按语句解析。

完整可运行示例见 [examples/foreach_demo.c](examples/foreach_demo.c)：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/foreach_demo.c
```

### 元组 `(T1, T2, ...)`

```c
(int, int) divmod(int a, int b) { return {a / b, a % b}; }  /* 返回多值 */
(int, double) average(int a, int b) { return {(a + b) / 2, (a + b) / 2.0}; }

(int q, int r) = divmod(17, 5);     /* 解构声明：一次声明多个变量 */
(a, b) = divmod(100, 7);            /* 解构赋值：目标可以是任意左值 */
(int high, _) = divmod(100, 7);     /* '_' 丢弃这个元素 */

(int, int) t = {1, 2};
t[0] = 9;                           /* 元素用常量下标读写 */
printf("%d\n", t[1]);
```

速查：

| 写法 | 行为 |
|---|---|
| `(T1, T2)` / `(T1, T2, ...)` | 元组类型，至少两个元素；就是一个布局固定的 struct |
| `(int, int) f(void) { return {q, r}; }` | 返回多值；`return {…}` 也适用于 struct / union |
| `(T1, T2) v = {a, b};` | 定义元组变量（和 struct 的初始化式一样） |
| `t[0]` / `t[1]` | 元素访问；下标必须是**整数常量**且在范围内 |
| `(int q, int r) = f();` | 解构声明：元素类型写全，一次声明多个局部变量 |
| `(a, b) = f();` | 解构赋值：目标是左值（变量、数组元素、`->`/`.` 成员） |
| `(int q, _)` / `(_, int r)` / `(q, _)` / `(_, r)` | `_` 丢弃对应元素（不取名、不生成加载） |
| `(int, int)!error parse(...)` | 元组也可以作 `!error` 的返回类型 |
| `async (int, int) f(...)` + `(int a, int b) = await f(...);` | `await` 的结果可以直接解构 |
| `go g((int, int) t)` | 元组按值穿过线程边界（同 struct） |

规则：

- 元素类型必须**完整**、非 `void`、非函数类型、非变长数组、非数组，并且
  **在文件作用域可见**（元组用一个内部 tag，函数里定义的 struct 不行）；
  `error` 不能当元素；数组不可赋值，需要时用指针、切片 `T[:]` 或包一层 struct；
- 元组按值传递、按值返回、可以赋给另一个兼容的元组，也可以当参数、返回值、
  结构体成员、数组元素；元素类型可以不同（`(char *, int)`、`(int, double)`），
  也可以嵌套（`((int, int), int)`）；
- 解构只能写成**语句**：`(int q, int r) = f();` 或 `(q, r) = f();`。要么全是
  “类型 + 名字”，要么全是左值，混着写会当成普通表达式而报错；元素个数必须和
  元组一致；全是 `_` 是错误（那等于丢掉整个值）；
- 因为它是语句，所以 `if (c) (a, b) = f();`、`for (…) (a, b) = f();`、
  `switch (c) { case 1: (a, b) = f(); }`、语句表达式 `({ … })` 和
  `defer (a, b) = f();` 这些地方都能直接用（不需要额外加花括号）；
- `_` 只在解构模式里有特殊含义，别处仍是普通标识符：`int _ = 5;`、
  `(int, int) _ = f();` 都照常工作；
- `(…)` 形式的老 C 写法不受影响：`(void)printf(…)`、`(int)x`、`(T)(x)`、
  `(struct S){…}`、语句表达式 `({…})`、逗号表达式 `(a, b)` 都还是原来的语义
  ——只有当括号里是一串“类型[, 类型…]”时才按元组类型解析；
- `#pragma pack` **不影响**元组：布局在类型构造时按自然对齐算好并缓存，
  所以同一个元组在任何 TU、任何 pragma 状态下都是同一个 ABI。

元组不引入任何新的 ABI：它就是一个普通 struct，参数和返回值都走已有的
struct 传递路径，所以和等价的 struct 可以互操作：

```c
/* a.c （-std=c6） */     (int, int) f(int a, int b) { return {a / b, a % b}; }
/* b.c （普通 C）   */     struct pair { int q, r; };
                           struct pair f(int a, int b);
```

`tests/abi/tuple.c` + `tests/abi/tuple_plain.c` 就是这么验证的：两个 TU 分别
用元组和 struct 编译，链接后互相调用。

已知的边角（都不影响上面的用法）：

- 元组类型可以写进 `interface` 的方法声明，但接口转换点目前只支持标量签名
  （`Splitter sp = h;` 会报 `'{' expected`）——这是元组之前就有的缺口，struct
  返回值一样会撞上；
- 复合字面量外面再套一层括号时，下标要写在括号**外**：
  `((int, int){1, 2})[0]` 可以，`((int, int){1, 2}[0])` 会报 `')' expected`。

完整可运行示例见 [examples/tuple_demo.c](examples/tuple_demo.c)：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/tuple_demo.c
```

### async / await（无栈协程）

```c
async int add(int a, int b) { return a + b; }

async int worker(void)
{
    int got = 0;
    await connect();               /* 语句式 */
    await send();                  /* 语句式 */
    got = await recv();            /* 赋值式 */
    return got;
}

int main(void)
{
    struct worker_task *t = worker();   /* 调用只建任务，不执行函数体 */
    int rc = __c6_co_wait(t);           /* 驱动到完成，0 = 正常结束 */
    printf("%d\n", t->val);
    __c6_co_destroy(t);
    return rc;
}
```

`async` 和无栈协程的区别在于**没有自己的栈**：函数体仍然跑在调用方的
栈上，`await` 处把已经用到的帧、defer 记录和错误槽存进任务，然后
`return` 回驱动方；恢复时把帧原样拷回同样的偏移，再跳到那个恢复点。
所以局部变量、`defer`、`err` 状态在挂起前后完全不变，也不需要上下文
切换。

速查：

| 写法 | 行为 |
|---|---|
| `async T f(…)` | 声明一个协程；调用 `f(…)` 得到 `struct f_task *` |
| `await f(…)` | 建子任务、推进它、等它完成后取 `val` |
| `await c6_io(fd, ev)` | 唯一的叶子挂起原语：等 `fd` 就绪，值是就绪掩码 |
| `t->val` / `t->pN` | 结果值 / 第 N 个实参（任务结构由声明推导） |
| `__c6_co_wait(t)` | 驱动到完成；`0` = 正常结束，`-1` = 子任务以 `err` 结束 |
| `__c6_co_destroy(t)` | 释放任务槽（含未完成的子任务） |
| `co f(…)` | 顶层协程：建完立刻跑到第一个必须等待的点，然后返回 |
| `task t = f(…)` | 建任务并拿句柄；作用域退出时自动 `__c6_co_destroy(t)` |
| `__c6_co_run()` | 把 `co` 出去的任务都跑完；返回其中以 `err` 结束的个数 |

规则：

- `async` 是声明说明符，和 `static` / `inline` 同级；只能定义在**文件
  作用域**，不能嵌套、不能 `async inline`、不能是 `main`；
- 签名必须是能从声明写回任务结构的类型：标量、枚举、指针、`T[:]`、
  文件作用域的 struct/union。数组、VLA、函数指针参数都会报错；
- 三种写法都可用：`await f();`、`a = await f();`、`await f(), a = await g();`
  也可以写在参数里（`f(a, await g())`）和条件里
  （`while (await c6_io(fd, C6_CO_EV_READ) > 0)`）；
- 挂起点上压在求值栈里的中间值必须落在**变量**里。帧槽会随帧一起恢复，
  寄存器不会——不过调用点本来就会把活寄存器落到帧槽，所以
  `x + await f()` 也能用；
- `await` 不能写进 `defer` / `errdefer` 体和 `sizeof` 里；
- `co f(a);` 和 `task t = f(a);` 的操作数都必须是一次 `async` 调用：
  `co` 建的是**顶层**任务（不归调用者、不做作用域回收，交给
  `__c6_co_run()`），`task` 建的任务归作用域管；
- `co` 是**同步起点**：任务在 `co` 语句处就开始跑，跑到第一个必须等待的
  点才把控制权交回来。`await c6_io` 总要停一轮（即使 fd 已经就绪，唤醒是
  下一轮 `select()` 的事）；`await` 子任务不算等待，所以不碰 fd 的
  `co f();` 会在 `co` 语句里跑完并当场回收槽位；
- `co`、`go`、`task` 是**软关键字**：只有在语句开头、后面紧跟一个标识符
  时才是关键字。`int go = 1;`、`go = 2;`、`x = go;`、`go(x);`、
  `typedef int co; co v;`、`task` 做变量名和 `go:` 标签都照常工作；
- `c6_io` 只有 `await c6_io(fd, ev)` 一种写法；`ev` 是
  `C6_CO_EV_READ` / `C6_CO_EV_WRITE`；
- `c6_io` 的 `fd` 必须在 `[0, 1024)` 内；越界的任务按失败回收，
  `__c6_co_wait` 返回 `-1`，`__c6_co_run` 计一次失败；
- 运行时池是**每线程**一个固定池（64 个任务、每个 4 KiB 帧、每个 64 条
  defer 记录），`__c6_co_wait` 和 `__c6_co_run` 都用 `select()` 等 fd，
  `__c6_co_run` 对顶层任务做轮转。协程本身是协作式、单线程的；要真并发
  用 `go`。

完整可运行示例见 [examples/async_demo.c](examples/async_demo.c)
（一次 HTTP 请求，传输层用 `pipe()` 代替 socket，离线可跑）：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/async_demo.c
```

### go（OS 线程）

```c
int download(int id)               /* 普通函数，阻塞也没关系 */
{
    defer printf("worker %d 结束\n", id);
    printf("worker %d 开始\n", id);
    sleep(1);
    return id;
}

int main(void)
{
    go download(1);                /* pthread_create，立刻往下走 */
    go download(2);
    printf("main 继续跑\n");       /* 上面两行可能还没打印 */
    return 0;                      /* 退出前编译器插入 __c6_th_join_all() */
}
```

`go f(a);` 是一条**语句**，不产生值：

| 写法 | 对函数的要求 | 引擎 | 返回 | 真并发 | 句柄 |
|---|---|---|---|---|---|
| `co f(a);` | 必须是 `async` | 无栈协程（帧拷贝） | 跑到第一个真正要等的地方 | 否 | 无（交给顶层队列） |
| `task t = f(a);` | 初始化式是 `async` 调用 | 同上 | 同上 | 否 | `t->wait()` |
| `go f(a);` | 任意普通函数 | `pthread_create` | **立刻** | **是** | 无 |
| `thread h = go f(a);` | 同上 | 同上 | 立刻 | 是 | `h->join()` |

句柄的方法是同一套写法，语义按引擎各算各的：

```c
task t = fetch(url);      t->wait();   /* 驱动事件循环直到它结束（顺带推进别的任务） */
thread h = go download(1); h->join();  /* 只等这一个 OS 线程，别的都不动 */

if (t->done()) ...        /* 结束了没：不驱动、不阻塞 */
if (t->kind() == C6_CO_KIND_ERR) ...   /* 失败的任务：wait() 也返回 -1 */
int n = (int)h->value();  /* 线程函数的返回值（整数或指针） */
```

名字不同是有意的：`t->wait()` 会推进整个事件循环，`h->join()` 是纯阻塞。

规则：

- 参数在 `go` 语句处**求值一次**，按值复制到堆上的参数块，线程解开参数块
  再调用 `f`；之后改局部变量不影响线程。支持标量、枚举、指针、`T[:]`、
  文件作用域的 struct/union；
- 不能 `go` 一个 `async` 函数（用 `co`，错误信息会提示）、不能 `go`
  变参函数、不能 `go` 带 `!error` 的函数（错误没人接）、不能通过函数指针
  `go`；
- 被调函数只要在调用点有个**文件作用域的声明**就行，定义可以在任何一个
  翻译单元；跳板按被调函数生成，每个 TU 各生成一份（`static`），没有被
  `go` 用到的原型不产生任何代码；
- 线程由**启动它的线程**回收：`main` 结束时编译器插入
  `__c6_th_join_all()`，别的线程在退出时回收自己的子线程，所以
  `-run` 也不会在线程还在跑的时候释放 JIT 内存。需要中途等待时可以自己
  调用 `__c6_th_join_all();`；
- 需要等某一个线程就用句柄：`thread h = go f(a); h->join();`（重复
  `join()` 幂等，`h->done()` 不阻塞，`h->value()` 取被调函数的返回值，
  整数或指针）。句柄是**启动者私有**的，不要跨线程传；作用域退出时自动
  归还，没 `join` 也不漏 —— 线程本身仍旧由启动者回收；
- 每个线程有自己的 defer 栈、错误槽和协程池（`pthread_key`，不是
  `__thread`，`-run` 不支持 TLS），所以线程里照常写 `defer`、`err`、
  `async`/`await`、甚至再 `go`；
- 运行时只用 `pthread_create` / `pthread_join` / `pthread_key_*`；
  glibc 2.34 起这些都在 libc 里，老系统需要 `-lpthread`；
- PE 目标还没有线程支持，`go` 在那边会触发 trap。

完整可运行示例见 [examples/thread_demo.c](examples/thread_demo.c)：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/thread_demo.c
```

### 托管引用（引用计数 GC）

`@T` 是**托管引用**（指向 `T` 的裸指针，多了一个 `@`）：对象的生命由引用
计数决定，最后一个引用消失时释放。没有标记清扫、没有 STW —— 计数就在对象头
里，加减都是普通的内存写，只有 `shared` 才走原子指令。

```c
struct Node { int v; own @Node next; };

void f(void)
{
    @Node a = new Node;      /* 计数 1，作用域退出时自动释放 */
    @Node b = new Node;
    a->v = 1;
    b->v = 2;
    a->next = move(b);       /* 独占链：b 的槽交出去，链由 a 带着走 */
    a = NULL;                /* 想提前放手就赋空，不需要 reset() */
}                            /* a 的链在这里整条释放 */
```

| 写法 | 含义 |
|---|---|
| `@T p` | 强引用，计数，可以有多个 |
| `shared @T p` | 强引用，计数是**原子**的，可以跨线程；对象要用 `new shared T` 建 |
| `weak @T p` | 弱引用，不计数；解引用前必须先升级（`@T q = p;`） |
| `own @T p` | 独占，不计数；持有者死亡时整条链递归释放；只能接 `new` 或 `move()` |

规则：

- 托管引用只能存进托管槽（局部、字段、元素、全局都算）；`T *p = managed;`
  只允许**立即使用**，保存会被拒（借出的指针没有计数，保存下来就是 UAF），
  新引用存进裸指针同样报错；
- 含托管字段的结构体不能按值拷贝/传参/返回（`memcpy` 也是），只能 `new`；
- 形参是**借用**：被调者不计数、不释放，也不能改写（要留住就拷进自有局部
  `@T m = n;`）；`async`/`go` 的边界只能传 `shared @T`；
- `move(p)` 之后 `p` 的槽就空了：在重新赋值之前再读它、比较它、解引用成员、
  传给函数、或者再 `move` 一次，都是**编译错误**（E-GC17）——空值解引用就是
  运行期崩溃。重新赋值（`p = new T` 或 `p = NULL`）之后照常使用；
- **环是编译错误**。类型图上出现自环或互环直接报错，出路只有两个：把反向
  边改成 `weak`，或者把子链改成 `own`。这也顺带把「父→子强、子→父弱」和
  「树用 own」逼成了默认写法；
- 析构函数写成方法 `void drop(struct T *self)`：最后一个引用消失时跑一次，
  在字段释放**之前**，不能失败、不能挂起、不能手动调用。

```c
struct File { int fd; };
void drop(struct File *self) { close(self->fd); }
```

`async`/`go` 的任务**自己持有**一份参数和返回值（进任务 +1、任务销毁 −1），
所以「创建者先放手、任务后跑完」是安全的；没跑完就被销毁的任务会回卷
（跑 `errdefer`/`defer`/释放），不会漏引用。

完整可运行示例见 [examples/gc_demo.c](examples/gc_demo.c)：作用域释放与
`p = NULL`、`new T[n]` 的逐元素析构、`own` 子树配 `weak` 父指针、`move()`、
`shared` 跨线程与 `go`、`async` 任务持有参数和返回值，最后用
`__c6_gc_live()` 自证「全程没有泄漏」（`rc=0` 才算过）：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/gc_demo.c
```

代价与尺寸（x86_64，热缓存，跨 TU 不内联）：`@T` 的一次加/减约 2.2 ns，
`shared @T` 约 8.4 ns（原子指令 + helper 调用）；每个对象 32 字节头；
作用域退出时释放是**内联**的，约 2.5 ns/引用。对照同机 `std::shared_ptr`
的 6.3 ns/操作：单线程 `@T` 更快，`shared @T` 同量级。

和 C++ 智能指针的差别：`@T` 就是裸指针（8 字节、能过 C ABI，`shared_ptr`
是 16 字节加一个控制块）；拷贝被编译器限制在托管槽之间（`shared_ptr` 随手
可拷）；环不会静默泄漏 —— 编译期就报错；另外 `p = NULL` 就是 `reset()`，
不额外引入成员函数。详细的设计与取舍见 [docs/gc-design.md](docs/gc-design.md)。

### #macro / __local

```c
#macro swap(a, b) {
    typeof(a) __local tmp = (a);   /* tmp 按展开序号重命名，不会捕获调用者变量 */
    (a) = (b);
    (b) = tmp;
}
```

## 测试

c6 的测试全部在 `tests/` 下，都用 c6 自己的 `test` 块写：

```
tests/
├── run-tests.sh      运行器（make selftest 调它）
├── defer.c           defer 的时机、顺序与作用域
├── err.c             err / errdefer / 清理顺序
├── try.c             try / catch
├── test-block.c      test 块与 -test 模式本身
├── macro.c           #macro 与 __local 名字卫生
├── package.c         package / import / pkg.member
├── method.c          结构体方法（含不同类型的同名方法与 libc 方法）
├── interface.c       结构化接口
├── slice.c           切片
├── tuple.c           元组（与 tu/tuple.c 一起跑）
├── tuple_error.c     元组 / struct 作 !error 的返回类型
├── tuple_async.c     元组 + async / await
├── tuple_thread.c    元组 + go（OS 线程）
├── async.c           async / await / co / task
├── thread.c          go（OS 线程）
├── gc.c              托管引用：分配、存储、作用域释放、数组
├── gc_field.c        字段释放、嵌套结构体、长链迭代释放
├── gc_type.c         shared / weak / own 的类型语义
├── gc_drop.c         析构方法 drop（含数组元素与 go 线程）
├── gc_thread.c       shared 跨线程：实参块持有、交接、并发加减
├── gc_async.c        async 任务的参数/返回值持有 + 销毁挂起任务时回卷
├── gc_own.c          own 独占链/子树 + move()
├── typename.c        裸 tag 类型名（省略 struct / union / enum）
├── tu/               跨翻译单元的第二个 .c（同名的主文件会一起编译）
├── abi/              ABI 兼容：同一份代码用 -std=c6 和纯 C 各编译一个 TU，
│                     链接后运行（`// EXIT <n>` 声明期望的退出码）
├── exit/             退出码（`// EXIT <n>` 声明期望的进程退出码）
└── errors/           编译期诊断（每个片段一个 -D 开关）
```

```sh
make selftest                       # 跑全部
tests/run-tests.sh                  # 同上
tests/run-tests.sh slice package    # 只跑这几个
tests/run-tests.sh --list           # 列出会跑的文件
```

单个文件也可以直接跑：

```sh
./tcc/tcc -B tcc -std=c6 -test tests/err.c
./tcc/tcc -B tcc -std=c6 -test tests/slice.c tests/tu/slice.c
./tcc/tcc -B tcc -std=c6 -test -test-filter=切片 tests/slice.c   # 按描述过滤
```

输出形如：

```
ok   err: 18 test(s), 0 failure(s)
ok   errors/slice: 12 个诊断片段
...
run-tests: 23 通过, 0 失败
```

`-test` 模式下 `main` 可有可无（运行器不调用它）。

### 编译期诊断怎么写

`tests/errors/*.c` 用 `#if defined <片段名>` 把每个报错场景放在一个片段里，
用注释声明该片段必须产生的诊断，不需要维护行号：

```c
// EXPECT test_void_element: error: '[:]' cannot have a 'void' element type

#if defined test_void_element
void f(void[:] s);
int main(void) { return 0; }
#else
int main(void) { return 0; }      /* 不带 -D 时必须编译并运行通过 */
#endif
```

运行器为每个 `EXPECT` 片段跑一次 `-D<片段>`，检查 stderr 含声明的文本，
再检查不带 `-D` 时该文件能编译并运行通过。

两条额外的约定：

- `tests/tu/<同名>.c` 存在时会作为**第二个翻译单元**一起编译（跨 TU 的环
  检测这类诊断要两个单元才看得见；`-run` 只接一个源文件，所以这种情况下
  运行器改用编译 + 链接）；
- `// NATIVE-ONLY <片段>` 标记该片段只在本机后端成立，`BACKEND=c99` 时跳过
  （例如跨 TU 环检查 —— C99 后端只输出 C 源码，没有链接期）。

### 完整回归

```sh
make test          # 扩展模式：上游 tests2 全量
make test-plain    # 关闭 C6_EXT 重建后再跑一遍，验证“扩展关闭零行为变化”
make c6selfcheck   # 用 -std=c6 编译 tcc 自己的源码，编出可运行的编译器
make test-all      # selftest + tests2 + c6selfcheck
```

自举（编译器编译自己，产物在 `build/selfhost/`，`make clean` 会一起删掉）：

```sh
make selfhost      # c6 路线：tcc 编译 tcc.c -> stage1，stage1 再编一次 -> stage2
make c99selfhost   # C99 后端路线：tcc 生成 C -> 宿主 gcc 编出 stage2 -> stage3
```

两条路线都会把两次编译得到的目标文件 `cmp` 一遍（bootstrap fixpoint），
再用最终的 stage2/stage3 编译并运行一个小程序：只有目标文件逐字节相同、
编出来的编译器真能编程序，自举才算通过。

`make c6selfcheck` 是"变量必须初始化"那条规则最严格的回归测试：tcc 自己
的源码也要满足它，所以整份源码都用 `-std=c6 -DC6_EXT` 编译一遍（原生目标
文件 + C99 后端产物；带 `C6_EXT` 才会把扩展实现那半边也纳入检查），再把
编出来的编译器连起来跑 `-v`。两步编译都不许有诊断输出，出现警告即失败。

> 切换模式会先 `make clean` 重建，因为 `C6_EXT` 是编译期开关。
> 三个目标都会先跑 `bootstrap`，只在 `tcc/` 还没配置时执行一次 `./configure`。

## 实现说明

- 设计文档：[docs/plan.md](docs/plan.md)（第 11 节为各阶段实现状态与限制）。
- 语法规范：[docs/grammar.md](docs/grammar.md)（EBNF 增量语法、静态约束与重写规则）。
- 运行时状态（defer 记录栈、错误槽、协程池、子线程链表）是**每线程**一份，
  通过 `pthread_key` 查找而不是 `__thread`：`tcc -run` 不支持 TLS。`go`
  出去的线程拿到自己的一份，所以线程里可以照常写 `defer`/`err`/`co`。
- 方法必须在调用点之前声明或定义（TCC 单趟编译的固有限制）；接口一致性检查发生在
  第一次转换点，因此方法也要在转换点之前可见。
- 跨 TU：头文件里用 `package NAME { ... }` 声明 API；接口的方法声明放头文件里，
  vtable 由每个 TU 按需自行生成。
- 方法符号是 `<结构体>_<方法>`，源码里写的名字另外发一个弱别名，所以
  `f(p)` 这种普通调用照旧可用，不同类型也可以有同名方法；`static` / `inline`、
  `__` 开头的名字、系统头文件里的声明、匿名结构体的方法，以及在 `package` 里
  声明的方法保留原名。
- 裸 tag 类型名在 `parse_btype()` 里解析：普通命名空间命中（变量/函数/枚举常量/typedef）
  就按老 C 语义走，否则再看 tag 命名空间；解析结果与写 `struct TAG` 完全同一个
  `CType`，所以 `sizeof`、方法注册、切片元素检查都自动一致。因为 tag 与普通符号
  分属两个命名空间，**同名时必须写 `struct`**（`struct stat` + `stat()` 就是例子）。
- 变量初始化检查在**定义点**做，不做数据流分析；VLA 是唯一豁免
  （标准 C 禁止给变长数组写初始化器）。
- 元组复用 `VT_STRUCT` 和内部 tag `__c6_tuple_N`，布局在构造类型时按自然对齐
  算好（不读 `pack_stack_ptr`），因此 `#pragma pack`、成员顺序、对齐都不会改变
  它的 ABI；元素名 `__c6_t0`…只为实现细节，对外只有 `t[i]`。
- 切片是普通 `static`/自动对象，没有隐藏状态也没有 GC（托管引用 `@T` 是
  另一回事，它的计数在对象头里）：越界检查由
  `libtcc1` 的 `__tcc_slice_index` / `__tcc_slice_sub` 完成，元素类型不参与
  ABI，因此两个 TU 只要都写 `T[:]` 就能互通。

## 上游与许可

TCC 源码及本仓库对它的修改均遵循 **LGPL-2.1**（见 `tcc/COPYING`）。
上游基线固定在 `2d3eea2`（`origin/mob`）。

## C99 output back-end (`-c99`, `-o *.c`, `-o <dir>`)

`tcc/c99-gen.c` is a second code-generator back-end: it reuses the complete
front-end and writes **portable C99 source** instead of machine code.  It is
built into the normal `tcc/tcc` binary; there is no separate compiler.

    make build                               # one tcc, both back-ends
    tcc/tcc -B tcc -o out.c prog.c           # prog.c -> out.c (.c suffix selects C)
    tcc/tcc -B tcc -c99 -o out.c prog.c      # same, explicit
    cc -std=c99 -o prog out.c && ./prog      # compile the result with any C compiler

    tcc/tcc -B tcc -c -o out/ a.c b.c        # out/a.c, out/b.c (one .c per TU)

    make c99test                             # same cases as selftest, via the C back-end

`make c99test` runs the same corpus as `make selftest`: every `tests/*.c`,
`tests/errors/*.c`, `tests/exit/*.c`, `tests/abi/*.c` and `tests/c99/*.c`
case goes through the same `tcc/tcc` (asked for C source with `-o ...c`) and
then through
`gcc -std=c99 -pedantic -Wall -Wextra -Werror -pthread`, with the same
exit-code, output and diagnostic assertions.  Diagnostics that only differ
because they are produced by the C-output back-end are declared with
`// EXPECT-C99` in the `tests/errors/*.c` file; everything else is shared.
`C99CC=clang make c99test` uses another host compiler, and
`BACKEND=c99 tests/run-tests.sh` runs the same thing directly.
`tests/c99/c99test.c` additionally exercises plain C and all the c6 features
listed below in one program.

`-c99` selects the output back-end; it is not a language-standard flag, and
the language being compiled stays C.  The native binary contains both
generators: the C one is selected by `-c99`, or automatically when `-o` names
a `.c` file or an existing directory (or a path ending in `/`).  So
`tcc/tcc -B tcc -o out.c prog.c` writes C source; with `-c99` and no `-o`
the output defaults to `a.c`.  `-c` means "one generated C file per
translation unit", and `-test` together with a `.c` output writes the
generated test-runner file.  `-o <dir>` gives every input translation unit its
own `<dir>/<input>.c`, so `-c -o out/ a.c b.c` produces `out/a.c` and
`out/b.c` without any `-c99`.  Preprocessing (`-E`) and linking actions
(`-shared`, `-r`, `-run`) keep their native meaning; asking the C back-end for
a machine-code/link action is an error instead of empty output, and an output
file that would overwrite one of its own inputs is refused.  `-B tcc` is
required so that the tool finds tcc's own `include/`.  The multiarch system
include directory is added from `CONFIG_TRIPLET` in `config.h`; if your host's
triplet is not detected, add `-I/usr/include/$(cc -print-multiarch)` by hand.
`make build` needs `objcopy` (binutils; `llvm-objcopy` works too) to localize
the embedded compiler's symbols.

The generated file only needs `memcpy`/`memmove`/`memset` from `<string.h>`:
the vstack registers become a `union` array `__c99_r[8]`, the stack frame is
the byte array `__c99_frm.f` addressed by tcc's own local offsets, comparisons
are materialised into `__c99_cnd`, and data sections are emitted as byte arrays
that `__c99_init()` patches from the relocation table at start-up.

Plain C programs work: integers, floats, pointers, arrays and string literals;
locals and globals; `if`/`while`/`for`/`do`/`switch`; recursion and direct and
indirect calls; static functions; `?:`, bit operations, shifts and all type
casts; structs, unions and functions returning structs; variadic function
definitions (`va_start`/`va_arg`/`va_end`, and passing the `va_list` on to a
host function such as `vsnprintf`), with the x86-64 SysV `va_list` layout.

Symbols the unit only imports (libc calls, and functions or objects of another
translation unit) are declared once, under a private `__c99_ext_N` name bound
to the real linker symbol with an `__asm__` label; the generated code reaches
them through a `#define` of the source name, which is dropped again before the
runtime prelude.  The declaration block can then keep the compiler's own idea
of a prototype -- tcc has one 64 bit integer type for `long` and `long long`,
and every struct it sees is an opaque byte block -- without colliding with the
host's declarations when the prelude includes `<stdlib.h>` and `<pthread.h>`.
A plain `cc out.c` (no `-w`, no extra include flags) is enough.

c6 programs work too.  A c6 translation unit (`-std=c6`) carries a portable C
implementation of the defer/error/slice/coroutine/thread runtime, appended to
its own output file.  It is ordinary C99 plus POSIX (`pthread` and `select`
for `async`/`await`, `go` and `c6_io`); no `libtcc1` is needed.  The runtime
symbols have weak linkage on GCC/Clang so that all generated translation units
of one program share a single runtime (errors, coroutines and joins cross TU
boundaries); compilers without weak symbols fall back to one private copy per
TU.  Features covered by `tests/*.c` include `defer`/`errdefer`, `err`,
`try`/`catch`, slices, `foreach`, tuples, methods/interfaces, `typename`,
`async`/`await`, `co`, `go` and `test` blocks:

    tcc/tcc -std=c6 -B tcc -o out.c prog.c
    cc -std=c99 -pedantic -Wall -Wextra -Werror -o prog out.c
    ./prog

A generated file with `test` blocks but no `main` gets a synthesized `main`
that runs them; a file with relocations but no `main` gets an
`__attribute__((constructor))` initializer instead.  With `-test` the
synthesized runner replaces a user `main` (renamed to `__c99_orig_main`),
mirroring the native `-test` runner; `make c99test` drives the test-block
files that way.

Structs of at most eight bytes containing only integer/pointer fields use the
x86-64 register-return ABI, so they interoperate with plain C (`tests/abi/`).
Other struct returns use a hidden result pointer chosen by this back-end:
that is consistent between c6 translation units, and for structs larger than
16 bytes it also matches the native ABI, but mixing 9--16 byte struct returns
with plain C is not supported.

Not supported (each gives a clear error instead of miscompiling): computed
goto and variable length arrays.  On non-POSIX hosts the generated runtime
still compiles, but `async`/`go`/`c6_io` need a platform port.

### Self-hosting through the C back-end

`make c99selfhost` is the end-to-end proof that the generated C is a working
compiler, not just working code:

    tcc/tcc -B tcc -DONE_SOURCE=1 -DC6_EXT -c99 -o build/selfhost/c99/tcc-c99.c tcc/tcc.c
    cc -std=c99 -o build/selfhost/c99/tcc-stage2 build/selfhost/c99/tcc-c99.c -lm -ldl -lpthread
    build/selfhost/c99/tcc-stage2 -B tcc -DONE_SOURCE=1 -DC6_EXT -c tcc/tcc.c -o stage3.o
    build/selfhost/c99/tcc-stage2 -B tcc -o tcc-stage3 stage3.o -lm -ldl -lpthread
    build/selfhost/c99/tcc-stage3 -B tcc -DONE_SOURCE=1 -DC6_EXT -c tcc/tcc.c -o stage4.o
    cmp stage3.o stage4.o                    # bootstrap fixpoint

`stage2` is the whole compiler (its own x86-64 back-end, linker and driver)
built by the host C compiler from tcc's C output; `stage3` is built by
`stage2` from the original sources, and the objects `stage2` and `stage3`
produce are byte identical.  `make selfhost` does the same round trip without
the C detour (`tcc` -> `stage1` -> `stage2`, objects compared) and is the
quick check that the c6 back-end still compiles its own sources.