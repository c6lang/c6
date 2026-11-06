# c6 —— 带语言扩展的 TinyCC

c6 是 [TinyCC](https://github.com/TinyCC/tinycc) 的一个分支，在不破坏原有 C 语义的前提下
加入了一组系统编程友好的语言扩展：

| 扩展 | 作用 |
|---|---|
| `defer { ... }` | 作用域/函数退出时逆序执行清理代码 |
| `errdefer { ... }` | 仅在错误路径上执行的 `defer` |
| `err(code, "msg")` | 设置错误槽、就地展开清理并直接返回 |
| `func() T!error` + `try` / `catch` | 声明可能失败的函数；`try` 原样传播，`catch e { ... }` 就地处理并清空错误槽 |
| `test "描述" { ... }` + `-test` | 顶层测试块与测试运行器 |
| `package` / `import` | 包与名字修饰（`pkg__sym`），`pkg.member` 访问导出符号，支持多级包名 |
| 结构体方法 | `ret f(struct T *self, ...)` 即为 `T` 的方法，可用 `p->f(...)` 调用 |
| `interface` | 胖指针（`{void *self; const VT *vt;}`）+ 自动生成的 vtable 接口抽象 |
| `T[:]` 切片 | 带长度与边界检查的数组视图（`{元素指针, 长度}` 两个机器字） |
| `#macro` + `__local` | 卫生宏：`__local` 标识符按展开序号重命名 |
| 变量初始化检查 | 每个变量定义都必须带初始化器（VLA 除外） |

扩展默认不启用，且是**编译期可裁剪**的：不定义 `C6_EXT` 构建出的编译器与上游 TCC 行为完全一致。

## 目录结构

```
.
├── docs/plan.md      设计与实施方案（含实现状态）
├── docs/grammar.md   扩展语法的 BNF/EBNF 规范
├── patches/          c6 扩展补丁（相对上游固定基线）
├── scripts/          bootstrap.sh：生成并搭建 TCC 工作树
├── tests/            c6 全部自测：*.c 是 test 块，errors/ 是编译期诊断
├── examples/         可运行示例（err_demo.c、try_demo.c、slice_demo.c）
├── Makefile          便捷入口（bootstrap / build / selftest / test / clean）
└── tcc/              TCC 源码树（由 bootstrap.sh 生成，不纳入本仓库）
```

## 快速开始

依赖：`git`、`make`、C 编译器（gcc/clang）、`binutils`。

```sh
# 1. 拉取上游 TCC，检出固定基线并应用 c6 补丁
./scripts/bootstrap.sh          # 等价于 make bootstrap

# 2. 构建（打开扩展）
make build                      # 等价于 make -C tcc -j EXTRA-DEFS=-DC6_EXT

# 3. 运行（-B tcc 让编译器找到未安装的 libtcc1.a 与运行时头文件）
./tcc/tcc -B tcc -std=c6 -run hello.c
```

如果网络需要代理，按环境要求导出即可（`git` 会读取）：

```sh
export http_proxy=http://localhost:2334
export https_proxy=http://localhost:2334
export all_proxy=http://localhost:2334
```

离线环境可以用 `C6_TCC_URL` 指向本地 TCC 克隆：

```sh
C6_TCC_URL=/path/to/tinycc ./scripts/bootstrap.sh
```

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
- `!error` 的函数返回类型必须能表示哨兵 `-1`（标量或枚举，不能是结构体）。

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

### 结构体方法

```c
struct Vec { int x, y; };

int vec_sum(struct Vec *self) { return self->x + self->y; }
int vec_twice(struct Vec *self) { return self->vec_sum() * 2; }

int main(void)
{
    struct Vec v = {3, 4};
    return (&v)->vec_twice();     /* 等价于 vec_twice(&v) */
}
```

- 文件作用域的函数声明/定义，只要**第一个参数是指向结构体（或联合体）的指针**，
  就注册为该类型的方法；调用 `p->f(a)` 被改写为 `f(p, a)`；
- 方法就是普通函数：**不改名、不改调用约定**（`fflush(FILE *)`、`gettimeofday(struct timeval *)`
  这类 libc 函数照旧可用），因此也能继续用 `f(p, a)` 调用；
- 只支持 `->`（`p->f()`）；`v.f()` 仍是普通成员访问，`v->f`（不带调用）会报
  `method 'f' must be called`；
- 结构体同名字段优先于方法；方法须在**使用点之前**可见（TCC 单趟编译）；
- 方法名共享 C 的全局命名空间，不同类型想要同名方法就放进不同 `package`
  （例：`package circle { int area(Circle *self) ... }` 与 `package rect { int area(Rect *self) ... }`
  可以共存，各自 `p->area()`）；
- 返回结构体的方法也支持（隐藏的 `self` 排在 sret 缓冲之后）；
- `T!error` 方法照常工作：`int!error f(struct T *self)` 可用 `try p->f()` / `p->f() catch e { ... }`。

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
  不依赖 libc，没有诊断文本；
- 静态初始化器（文件作用域或 `static` 局部）没有运行时代码，所以
  `int[:] g = arr;` 不支持，写 `int[:] g = { arr, 5 };`（两个字段都是常量，可用）；
- 只是普通 C 语义的补充：`int a[3]`、`int a[] = {…}`、`void f(int[])`、
  `typedef int T[]` 全部保持原样，`int[]` 并**不**是切片写法。

完整可运行示例见 [examples/slice_demo.c](examples/slice_demo.c)：

```sh
./tcc/tcc -B tcc -std=c6 -run examples/slice_demo.c
```

跨 TU 时两边各自写 `T[:]` 即可（布局固定），不需要额外声明。

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
├── method.c          结构体方法
├── interface.c       结构化接口
├── slice.c           切片
├── tu/               跨翻译单元的第二个 .c（同名的主文件会一起编译）
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
run-tests: 15 通过, 0 失败
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

### 完整回归

```sh
make test          # 扩展模式：上游 tests2 全量
make test-plain    # 关闭 C6_EXT 重建后再跑一遍，验证“扩展关闭零行为变化”
make test-all      # selftest + tests2
```

> 切换模式会先 `make clean` 重建，因为 `C6_EXT` 是编译期开关。

## 实现说明

- 设计文档：[docs/plan.md](docs/plan.md)（第 11 节为各阶段实现状态与限制）。
- 语法规范：[docs/grammar.md](docs/grammar.md)（EBNF 增量语法、静态约束与重写规则）。
- 运行时状态（defer 记录栈、错误槽）是普通全局变量：`tcc -run` 不支持 TLS，
  因此这一版是单线程语义。
- 方法必须在调用点之前声明或定义（TCC 单趟编译的固有限制）；接口一致性检查发生在
  第一次转换点，因此方法也要在转换点之前可见。
- 跨 TU：头文件里用 `package NAME { ... }` 声明 API；接口的方法声明放头文件里，
  vtable 由每个 TU 按需自行生成。
- 方法名共享 C 的全局命名空间；同名方法服务不同类型时用 `package` 隔离。
- 变量初始化检查在**定义点**做，不做数据流分析；VLA 是唯一豁免
  （标准 C 禁止给变长数组写初始化器）。
- 切片是普通 `static`/自动对象，没有隐藏状态也没有 GC：越界检查由
  `libtcc1` 的 `__tcc_slice_index` / `__tcc_slice_sub` 完成，元素类型不参与
  ABI，因此两个 TU 只要都写 `T[:]` 就能互通。

## 上游与许可

TCC 源码及本仓库的补丁均遵循 **LGPL-2.1**（见 `tcc/COPYING`）。
上游基线固定在 `2d3eea2`（`origin/mob`）。
