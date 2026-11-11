# 函数字面量与闭包

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> `void(int x) { ... }` 是表达式，值是函数指针；`closure RET (P…)` 把外层块作用域的名字按值捕获进 env（V2）。
>
> 相关：示例 `examples/fn_lit_demo.c6`；用例 `tests/fn_lit.c6`、`tests/closure.c6`；设计 [closure-design.md](../design/closure-design.md)

---

## 函数字面量 `void(int x) { ... }`

```c
void (*say)(int) = void(int x) { printf("%d\n", x); };  /* 显式声明 */
say(1);

void(int x) { printf("%d\n", x); }(2);                   /* 就地调用 */

f = int(int x) { return x * offset; };                   /* 推断声明 */
f(3);

qsort(v, n, sizeof v[0], int(const void *a, const void *b) {   /* 交给 C */
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
});
```

速查：

| 写法 | 行为 |
|---|---|
| `void(int x) { … }` / `int(int a, int b) { … }` | 字面量：一个表达式，值就是那个函数（退化成函数指针） |
| `void (*f)(int) = void(int x) { … };` | 用已有的函数指针类型接住 |
| `f = void(int x) { … };` | 推断声明：类型从字面量推出（`void (*)(int)`），等价于在 `f` 前写上那个类型 |
| `int(int x) { … }(3)` | 就地调用 |
| `int() { … }` | 无参（`()` 视作无参原型） |
| `int!error (int a, int b) { … }` | 可失败的字面量，配 `try` / `catch` 用 |
| `int (int n, ...) { … }` | 变参 |
| `struct P(int v) { … }` / `int[:](int[:] s) { … }` | 返回类型随便写，切片、元组、`@T` 都行 |
| `static int (*t[2])(int) = { { int(int v) { … } }, … };` | 文件作用域 / `static` 初始化器里也行 |

规则：

- **值就是函数指针**：不分配对象、不做引用计数、运行期不携带任何额外信息。
  所以它能存进任何同型函数指针、进函数指针表、当实参、当返回值、交给纯 C
  保存和回调——C ABI 一点没变（`tests/abi/fn_lit.c6`）。每个字面量各是一个
  函数：`a == b` 为假；
- **体里只看得见它自己的参数和文件作用域**。字面量的体是稍后编译的（和
  `static inline` 一样在翻译单元末尾发射），写在外层的局部变量、形参、块
  作用域的 `typedef` / tag 没法带进去，引用它们是编译期错误：

  ```c
  int x = 1;
  int (*f)(void) = int() { return x; };      /* 错误：捕获了 x */
  int (*g)(int)  = int(int n) { return n + offset; };  /* 可以：n 和文件作用域 */
  ```

  需要外层的值时把它作为参数传进来，或者写成捕获式闭包（V2，见下）；
- 字面量**没有名字**，也不能带存储类或 `async`：`void named(void) { … }`
  报 `a function literal has no name`，`static void() { … }` 报
  `a function literal cannot have a storage class`；
- 内部名字是 `__c6_fn_<序号>`（`static`），所以 `module` 不改写它、
  `-emit-header` 不导出它，符号表里只是本地的 `t`；
- 和 c6 的其它特性是正交的：体里可以 `defer` / `err` / `new` / 再写字面量，
  `static inline` 函数体和异步函数体里也能写字面量。

V1 的完整可运行示例见 [examples/fn_lit_demo.c6](../../examples/fn_lit_demo.c6)：

```sh
./tcc/c6 -B tcc -run examples/fn_lit_demo.c6
```

#### 捕获式闭包 `closure`（V2）

> 设计全文见 [docs/design/closure-design.md](../design/closure-design.md)，验收清单与
> 用例对照见 [docs/history/closure-todo.md](../history/closure-todo.md)。

```c
closure int(int) make_adder(int n)
{
    return int(int x) { return x + n; };   /* n 按值拷贝进 env */
}

int main(void)
{
    closure int(int) add5 = make_adder(5);
    return add5(1) != 6;
}
```

- 类型写成 `closure RET (参数…)`，`sizeof` 是一个机器字；值是 `@` 托管引用，
  隐藏对象里放 `{code, env}`；
- 体里自由出现的块作用域名字按值拷进 env（在字面量求值时快照）；体里改的
  是副本，外层变量不受影响；
- 无捕获的字面量仍是裸函数指针（V1 行为）；需要放进 `closure` 槽时自动包装
  成闭包；闭包不能变回函数指针；
- 闭包不能跨线程/任务，也不能 `shared`/`weak`/`own`；诊断 C90–C98。
