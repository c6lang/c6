# superc —— 带语言扩展的 TinyCC

superc 是 [TinyCC](https://github.com/TinyCC/tinycc) 的一个分支，在不破坏原有 C 语义的前提下
加入了一组系统编程友好的语言扩展：

| 扩展 | 作用 |
|---|---|
| `defer { ... }` | 作用域/函数退出时逆序执行清理代码 |
| `errdefer { ... }` | 仅在错误路径上执行的 `defer` |
| `err(code, "msg")` | 设置错误槽、就地展开清理并直接返回 |
| `func() T!error` + `try` / `catch` | 声明可能失败的函数；`try` 原样传播，`catch e { ... }` 就地处理并清空错误槽 |
| `test "描述" { ... }` + `-test` | 顶层测试块与测试运行器 |
| `package NAME { ... }` / `import NAME;` | 单文件与跨 TU 的包与名字修饰（`NAME__sym`） |
| `interface` / `impl` | 胖指针（`{void *self; const VT *vt;}`）+ vtable 的接口抽象 |
| `#macro` + `__local` | 卫生宏：`__local` 标识符按展开序号重命名 |
| 变量初始化检查 | 每个变量定义都必须带初始化器（VLA 除外） |

扩展默认不启用，且是**编译期可裁剪**的：不定义 `TCC_EXT` 构建出的编译器与上游 TCC 行为完全一致。

## 目录结构

```
.
├── docs/plan.md      设计与实施方案（含实现状态）
├── docs/grammar.md   扩展语法的 BNF/EBNF 规范
├── patches/          superc 扩展补丁（相对上游固定基线）
├── scripts/          bootstrap.sh：生成并搭建 TCC 工作树
├── tests/test.c      自测：用 superc 的 test 块覆盖 defer/err/try/catch
├── examples/         可运行示例（err_demo.c、try_demo.c）
├── Makefile          便捷入口（bootstrap / build / test / selftest / clean）
└── tcc/              TCC 源码树（由 bootstrap.sh 生成，不纳入本仓库）
```

## 快速开始

依赖：`git`、`make`、C 编译器（gcc/clang）、`binutils`。

```sh
# 1. 拉取上游 TCC，检出固定基线并应用 superc 补丁
./scripts/bootstrap.sh          # 等价于 make bootstrap

# 2. 构建（打开扩展）
make build                      # 等价于 make -C tcc -j EXTRA-DEFS=-DTCC_EXT

# 3. 运行（-B tcc 让编译器找到未安装的 libtcc1.a 与运行时头文件）
./tcc/tcc -B tcc -std=tcc-ext -run hello.c
```

如果网络需要代理，按环境要求导出即可（`git` 会读取）：

```sh
export http_proxy=http://localhost:2334
export https_proxy=http://localhost:2334
export all_proxy=http://localhost:2334
```

离线环境可以用 `SUPERC_TCC_URL` 指向本地 TCC 克隆：

```sh
SUPERC_TCC_URL=/path/to/tinycc ./scripts/bootstrap.sh
```

## 使用

扩展需要**编译器打开 `TCC_EXT`** 并且**每个翻译单元加 `-std=tcc-ext`**（该开关同时定义
`__TCC_EXT__`，方便写条件编译）。

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
./tcc/tcc -B tcc -std=tcc-ext -run examples/err_demo.c
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
- `!error` 的函数返回类型必须能表示哨兵 `-1`（标量或枚举，不能是结构体）。

完整可运行示例见 [examples/try_demo.c](examples/try_demo.c)：

```sh
./tcc/tcc -B tcc -std=tcc-ext -run examples/try_demo.c
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
./tcc/tcc -B tcc -std=tcc-ext -test tests.c               # 运行全部 test 块
./tcc/tcc -B tcc -std=tcc-ext -test -test-filter=add tests.c
```

运行器输出 `PASS:` / `FAIL:` 行与 `N test(s), M failure(s)`，全部通过时退出码为 0。

### package / import

```c
package math {
    int add(int a, int b) { return a + b; }
}

import math;

int main(void) { return add(1, 2); }   /* 也叫 math__add */
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

- **不开启扩展就完全不变**：不带 `-std=tcc-ext` 编译的翻译单元与上游 TCC
  逐字节一致，老项目（包括 TCC 自己的源码）不需要改任何一行；
- 扩展是**按翻译单元**开启的：一旦给某个 `.c` 加了 `-std=tcc-ext`，这个 TU
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

### interface / impl

```c
interface Shape {
    int area();
}

struct Circle { int r; };

impl Shape for Circle {
    int area(Circle *self) { return 3 * self->r * self->r; }
}

int describe(Shape s) { return s.area(); }   /* 结构体值隐式转换为胖指针 */

int main(void)
{
    Circle c = {2};
    return describe(c) == 12 ? 0 : 1;
}
```

`impl` 的方法签名会与接口声明比对：缺方法、参数个数、参数类型、返回类型不一致都会编译报错。

### #macro / __local

```c
#macro swap(a, b) {
    typeof(a) __local tmp = (a);   /* tmp 按展开序号重命名，不会捕获调用者变量 */
    (a) = (b);
    (b) = tmp;
}
```

## 测试

`tests/test.c` 用 superc 的 `test` 块自测 `defer` / `err` / `try` / `catch` 的语义：

```sh
make selftest                            # 跑 tests/test.c
./tcc/tcc -B tcc -std=tcc-ext -test tests/test.c
./tcc/tcc -B tcc -std=tcc-ext -test -test-filter=errdefer tests/test.c   # 按描述过滤
```

输出形如：

```
PASS: err 写入错误码与消息并返回 -1
PASS: errdefer 只在错误路径执行
...
21 test(s), 0 failure(s)
```

`-test` 模式下 `main` 可有可无（运行器不调用它）。

更完整的回归：

```sh
make test          # 扩展模式：tests/tests2 全量（含 156..165 的 superc 专项测试）
make test-plain    # 关闭 TCC_EXT 重建后再跑一遍，验证“扩展关闭零行为变化”
make test-all      # selftest + tests2
```

> 切换模式会先 `make clean` 重建，因为 `TCC_EXT` 是编译期开关。

## 实现说明

- 设计文档：[docs/plan.md](docs/plan.md)（第 11 节为各阶段实现状态与限制）。
- 语法规范：[docs/grammar.md](docs/grammar.md)（EBNF 增量语法、静态约束与重写规则）。
- 运行时状态（defer 记录栈、错误槽）是普通全局变量：`tcc -run` 不支持 TLS，
  因此这一版是单线程语义。
- `interface` 的 `impl` 必须先于使用点可见（TCC 单趟编译的固有限制）。
- 跨 TU：头文件里用 `package NAME { ... }` 声明 API；`impl IFACE for TYPE;` 只声明，
  vtable 由另一个 TU 提供。
- 变量初始化检查在**定义点**做，不做数据流分析；VLA 是唯一豁免
  （标准 C 禁止给变长数组写初始化器）。

## 上游与许可

TCC 源码及本仓库的补丁均遵循 **LGPL-2.1**（见 `tcc/COPYING`）。
上游基线固定在 `2d3eea2`（`origin/mob`）。
