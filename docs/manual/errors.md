# 错误处理：defer / errdefer / err / T!error / try / catch

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> `defer` / `errdefer` 在作用域退出时逆序清理，`err` 写错误槽并返回哨兵，`T!error` + `try` / `catch` 负责传播与就地处理；错误码按模块分号段。
>
> 相关：示例 `examples/err_demo.c6`、`examples/try_demo.c6`；用例 `tests/defer.c6`、`tests/err.c6`、`tests/try.c6`、`tests/err_ids.c6`

---

## defer / errdefer / err

```c
int __tcc_err_id_get(void);
const char *__tcc_err_msg_get(void);
void __tcc_err_clear(void);

int load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return err(1, "open failed");
    defer fclose(f);                       /* 正常退出、return、break 都会执行 */

    char *buf = malloc(4096);
    defer free(buf);                       /* LIFO：free 先于 fclose */

    errdefer printf("load: cleaning up after error\n");

    if (fread(buf, 1, 4096, f) == 0)
        return err(2, "read failed");      /* 返回 -1，errdefer 逆序执行 */
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

完整可运行示例见 [examples/err_demo.c6](../../examples/err_demo.c6)：

```sh
./tcc/c6 -B tcc -run examples/err_demo.c6
```

`err` 速查：

| 写法 | 行为 |
|---|---|
| `return err(id, msg)` | 写入错误槽（`flag=1`），逆序执行本帧的 `errdefer` 与 `defer`，然后返回哨兵 `-1`；`err(..)` 只能写成 `return err(..)` |
| `defer stmt;` | 任何离开作用域的路径都执行（正常 `return`、`break`、`continue`、`goto`、`err`） |
| `errdefer stmt;` | 仅在错误路径（本帧执行了 `err`）执行，正常返回时跳过 |


## 错误码是模块自己的号段

`err` 的 id 不是随手写的裸数字：模块里声明 `enum Error { ... }`，成员就落在
**这个模块自己的号段**里，跨模块不会撞：

```c
// app/db.c6
module app.db;

enum Error {          // 成员的实际取值（app.db 钉在 0x6100）：
    OK    = 0,        // 0x00000000 —— 0 永远是「没有错误」
    NOPEN = 1,        // 0x06100001
    BUSY  = 2,        // 0x06100002
};

int!error load(void)
{
    if (busy)
        return err(BUSY, "db is busy");
    return 0;
}
```

```
id = (area << 27) | (slot << 12) | code
```

| 号段 | 谁用 |
|---|---|
| `slot 0` | 不属于任何模块的文件（`main` 的 TU、`tests`）：`return err(1, "x")` 仍是 `1` |
| `slot 1..0x5FFF` | 自动池：`1 + fnv1a32(模块名) % 0x5FFF`，号是名字的纯函数 |
| `slot 0x6000..0x60FF` | std 内置池（`std.core.err`=0x6000，见 [docs/stdlib.md](../stdlib.md)） |
| `slot 0x6100..0x7FFF` | 钉号池：项目根 `errbase.def` 里的行 |

项目根有一个 `errbase.def`（一次 `-emit-errbase` 就生成，**建议进版本库**）：

```
# 格式：  <模块名>  <slot>       slot 取 0x6100..0x7FFF

app.db       0x6100
app.cli      0x6101
```

- 没列出来的模块按名字哈希自动拿号；列出来的号**永远不会变**。
- `-emit-errbase[=FILE]`：把这次编译能看见的所有模块一次钉住（已有的行原样
  保留，新模块按名字排序接在末尾），写完就停；产出的文件可以直接提交。
- `-errbase-file=FILE` 指定别的表（必须存在）；`-no-errbase` 完全不读表。
- 两个模块撞进同一个号段时**报错**，不静默：把其中一个钉进 `errbase.def`。
- 没有 `errbase.def` 又没给 `-errbase-file` 时，链接带模块的程序会自动写一份
  只有注释的空表 —— 空表不改变任何 id。

细节与诊断（C124–C128）见 [docs/reference/grammar.md](../reference/grammar.md) §8.1。

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


## `defer` 的运行期状态住在函数帧里

一个清理点（cleanup site）是**编译期的常量**：编号、所在作用域的深度、
是 `defer` 还是 `errdefer`、清理体的标签。运行期只有两样东西会变，而它们
都是**本帧的局部变量**：

- 哪些清理点已经注册——帧里的一块位图，函数入口清零；
- 正在服务哪个清理请求——两个 `int`（目标作用域深度 + 恢复点号），只在
  派发清理的时候写。

于是：

- **不需要任何线程级 / TLS 状态**：ISR 里、`spawn` 起来的线程里、协程
  （`async`）里、递归的每一帧都按同一套规则跑，互不干扰；
- 递归深度没有上限（旧实现的每线程记录栈上限 8192 条，超过就悄悄丢）；
- 同一个作用域实例里同一个清理点**最多跑一次**（位是幂等的），所以
  `goto` 回到块入口再经过一遍 `defer` 语句不会重复注册；
- 循环体里的 `defer` 是“每次迭代一个作用域实例”，所以每轮各跑一次；
- 挂起的 `task` 被销毁时按错误路径派发（mode 2），所以 `errdefer` 也会跑：
  被销毁的任务没有“正常结束”这回事；
- 向后跳转的 `goto`（标签已经定义）由 `goto` 语句自己发起清理请求，深度
  就是那个标签的作用域深度；向前跳转的 `goto` 落在标签处派发，不多花代码；
- 编译期上限：一个函数最多 **128 个清理点**（第 129 个报错，叫你把函数拆
  小），清理体最多嵌套 **8 层**（第 9 层报错）。
- 帧开销：c6 的翻译单元（`.c6`）里每个函数帧固定多 4 × `int` 位图 + 1 × `int` 已注册计数
  （序言里清零——单趟编译器没法在序言处知道后面会不会出现清理点）；有清理点
  的函数再加 2 × `int` 的派发状态，清理体每嵌套一层再加 2 × `int`。派发器
  本身也只在有清理点时才生成。旧实现在每个函数开头调一次
  `__tcc_defer_func_enter()`（一次调用 + `tcc_th()`），现在换成 5 条清零 store。
- 派发代价与函数里**总共有多少清理点无关**：每个请求在编译期就定好了入口
  （第一个可能匹配的清理点），"什么都没注册"的请求一条比较就返回，请求结束
  用二分找恢复点。实测 20M 次"退出一个带 defer 的块"：1 个清理点的函数
  0.18 s，41 个清理点的函数 0.21 s（改造前是 1.58 s）。


## `T!error` / `try` / `catch`

函数返回类型后面加 `!error` 表示“可能失败”。`err(id, msg)` 在这类函数里是
直接返回的简写；调用方可以用 `try` 原样传播，或用 `catch` 就地处理。

```c
/* 返回类型后面加 '!error'（也可以写在声明符后面：int parse(const char *)!error） */
static int!error parse(const char *s)
{
    if (!s)
        return err(1, "string is NULL");  /* 设置错误槽并返回哨兵 -1 */
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
| `return err(id, msg)` | 设置错误槽并立即返回；只能在 `T!error` 函数里这样写，单独写 `err(..)` 是错误 |

`e` 的类型就是 `error`（编译器预置的 `typedef struct __tcc_error { int id; const char *msg; } error;`）。
每次 `T!error` 调用之前都会先清空错误槽，`catch` 在进入处理块前再**快照并清空**一次，
所以连续两个 `catch` 不会串扰，也不会把之前遗留的错误误判成这次调用的失败。

约束（违反时编译报错）：

- `try` 只能出现在返回类型带 `!error` 的函数里，且操作数必须是一次调用；
- `catch` 的操作数也必须是一次调用，被调函数要返回 `T!error`；
- `catch` 处理块必须自己离开：`return` / `return err(id, msg)` / `goto`；编译器判断
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

完整可运行示例见 [examples/try_demo.c6](../../examples/try_demo.c6)：

```sh
./tcc/c6 -B tcc -run examples/try_demo.c6
```

语法规范（EBNF）见 [docs/reference/grammar.md](../reference/grammar.md)。
