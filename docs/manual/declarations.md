# 声明：省略 struct 与变量初始化

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> tag 本身就是类型名（`Vec v;`），以及「每个变量定义都必须带初始化器」这条硬规则。
>
> 相关：示例 `examples/typename_demo.c6`；用例 `tests/typename.c6`、`tests/enum_scope.c6`

---

## 省略 struct / union / enum

tag 本身就是类型名，不用再写 `struct`（`union` / `enum` 同样）。`new` 后面
也写标签本身：`new Pair` 可以，`new struct Pair` 目前会编过但运行时崩，别写。

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
- 只在 c6 的翻译单元（`.c6` 文件）里生效，`struct Vec v;` 永远可用。

#### 枚举成员写成 `Color.RED`

C 的枚举成员进的是普通标识符命名空间，两个枚举不能有同名成员；c6 让成员可以带上
枚举自己的 tag，于是每个枚举自带一层空间：

```c
enum Color { Color.RED = 1, Color.GREEN = 2 };
enum Fruit { Fruit.RED = 10, Fruit.APPLE = 20 };

int main(void)
{
    int n = Color.RED + Fruit.RED;      /* 11：两个 RED 各有各的值 */
    int m = GREEN + APPLE;              /* 22：没被抢占的裸名照旧可用 */
    int a[Color.GREEN] = { 0 };         /* 常量表达式 */
    if (n != 11 || m != 22 || sizeof a / sizeof a[0] != 2)
        return 1;
    switch (n) {
    case Color.GREEN: return 2;         /* case 里也能用 */
    case 11: return 0;
    }
    return 3;
}
```

- 限定符必须是这个枚举自己的 tag（C117），匿名枚举的成员不能限定（C118）；
- 两个枚举都要同一个裸名时，裸名被毒化（C119）：此后裸名报错，必须写
  `Color.RED` / `Fruit.RED`——它指谁不再取决于书写顺序；
- `Tag.成员` 是表达式，`if`/`while`/`for` 条件、`case`、数组长度里都能用，
  枚举里没有这个名字时报 C120。

细节见 [docs/reference/grammar.md](../reference/grammar.md) §3.8，用例见
[tests/enum_scope.c6](../../tests/enum_scope.c6) 与 [tests/errors/enum_scope.c6](../../tests/errors/enum_scope.c6)。

完整可运行示例见 [examples/typename_demo.c6](../../examples/typename_demo.c6)：

```sh
./tcc/c6 -B tcc -run examples/typename_demo.c6
```


## 变量必须初始化

扩展模式下，**每个变量定义都必须带初始化器**，否则编译报错
（`variable 'x' must be initialized`）。两处例外：`static` 变量和 VLA。

```c
int counter = 0;                  /* 文件作用域的定义也要写 */
static int instances;             /* static 变量豁免：C 保证它清零 */

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
| `int x;`（自动变量、全局、`for` 声明） | 报错 |
| `static int x;`（文件作用域或块作用域） | 合法：静态存储期由 C 清零，`= 0` 只是噪声 |
| `extern int x;` | 合法（声明，不是定义） |
| 函数形参、结构体/联合体成员、`typedef` | 合法（不受此规则约束） |
| `int a[n];`（VLA） | 合法：标准 C 不允许 VLA 带初始化器 |

规则是**定义点检查**，不做数据流分析：`int x; x = 1;` 一样报错。
诊断使用源码里书写的标识符，模块内的变量也报原名而不是 `mod__x`。

旧代码怎么处理：

- **不开启扩展就完全不变**：`.c` 文件编译出来的翻译单元与上游 TCC
  逐字节一致，老项目（包括 TCC 自己的源码）不需要改任何一行；
- 扩展是**按翻译单元**开启的：一旦把文件命名成 `.c6`，这个 TU
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
static int n;             ->  不用改（static 豁免）
int a[n];                 ->  不用改（VLA 豁免）
```

注意两个开销：

- 局部的大数组/大结构体加 `= {0}` 会真的清零（编译成一次 `memset`）；
  如果原来就靠后续 `read`/`memset` 填满，这次清零是新增开销；
- 在 TCC 里，**显式**写 `= {0}` 的全局/`static` 对象会落到 `.data`
  （文件里真的存一份零），而不带初始化器的对象在 `.bss`，不占文件空间。
  这也是 `static` 豁免的实际好处：常驻状态照原样写 `static int n;`，
  不必为了过检查把大缓冲区搬进 `.data`。

TCC 自己的源码也遵守这条规则（`cmake --build build --target c6selfcheck` 把 `tcc.c` 拷成 `.c6` 后再
按 c6 规则编译一遍来检查），因此 c6 规则编 tcc 自己不会撞上这条诊断；带
`-DC6_EXT` 也一样，`#ifdef C6_EXT` 里的扩展实现同样补了初始化器：

```sh
cp tcc/tcc.c build/tcc-c6.c6 && \
    ./tcc/c6 -B tcc -I tcc -DC6_EXT -c build/tcc-c6.c6 -o tcc.o  # 原生目标文件
./tcc/c6 -B tcc -I tcc -DC6_EXT -o tcc.c.c build/tcc-c6.c6       # C99 后端的输出
```

为此 `tcc/tcc.h` 里那批编译器全局量（`tok`、`file`、`int_type`……）改成了
"头文件里 `extern` 声明 + 各自 `.c` 文件里带初始化器定义一次"：它们没有
`static`，不能走豁免，只能不依赖"暂定定义"这种不带初始化器的写法。带
`static` 的编译器内部状态（模板粘性表、各类缓存）则直接写成
`static T *head;`。
