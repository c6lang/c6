# 元组与多返回值

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> `(T1, T2, ...)` 是函数多返回值：`return {a, b};`、`(int q, int r) = f();`，元素用常量下标 `t[0]` 访问。
>
> 相关：示例 `examples/tuple_demo.c6`；用例 `tests/tuple.c6`、`tests/tuple_error.c6`、`tests/tuple_async.c6`、`tests/tuple_thread.c6`

---

## 元组 `(T1, T2, ...)`

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
| `spawn g((int, int) t)` | 元组按值穿过线程边界（同 struct） |

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
/* a.c6 （c6）       */     (int, int) f(int a, int b) { return {a / b, a % b}; }
/* b.c  （普通 C）   */     struct pair { int q, r; };
                           struct pair f(int a, int b);
```

`tests/abi/tuple.c6` + `tests/abi/tuple_plain.c` 就是这么验证的：两个 TU 分别
用元组和 struct 编译，链接后互相调用。

已知的边角（都不影响上面的用法）：

- 接口的方法签名支持 struct / 元组作参数和返回值（`tests/iface_agg.c6`），
  只要类型在 `interface` 声明之前已经可见：`struct P;` 要写在 interface
  前面，否则那个 tag 只是原型作用域里的另一个类型（诊断会提示
  prototype-scope tag）；
- 复合字面量外面再套一层括号时，下标要写在括号**外**：
  `((int, int){1, 2})[0]` 可以，`((int, int){1, 2}[0])` 会报 `')' expected`。

完整可运行示例见 [examples/tuple_demo.c6](../../examples/tuple_demo.c6)：

```sh
./tcc/c6 -B tcc -run examples/tuple_demo.c6
```
