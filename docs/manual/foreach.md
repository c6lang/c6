# foreach 循环

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> 遍历数组、切片、整数区间，以及实现 `has()` / `next()` 协议的迭代器；头里声明绑定。
>
> 相关：示例 `examples/foreach_demo.c6`、`examples/foreach_iter_demo.c6`；用例 `tests/foreach.c6`、`tests/foreach_head.c6`、`tests/foreach_iter.c6`

---

## `foreach (T v = a) { ... }`

```c
int a[5] = {10, 20, 30, 40, 50};
int[:] s = a[1:4];

foreach (int v = a) { ... }             /* 只要值：v 是元素副本 */
foreach ((size_t i, int v) = a) { ... } /* 下标 + 值 */
foreach (size_t i = 0:5) { ... }        /* 区间 [0, 5)：只要下标 */
foreach ((size_t i, int v) = s) { ... } /* 切片：长度是求值那一刻的快照 */
foreach ((size_t _, int v) = a) { ... } /* '_' 丢弃这个绑定 */
foreach ((size_t i, int v) = obj.items) { ... } /* 结构体暴露的切片成员 */
foreach (int v = it) { ... }    /* 迭代器：it 的类型提供 has(T *)/next(T *) */
```

速查：

| 写法 | 行为 |
|---|---|
| `foreach (T v = 数组)` | 只要值：`v` 是元素副本 |
| `foreach ((T i, T v) = 数组)` | 下标 + 值：`i` 从 0 数到长度减一 |
| `foreach ((T i, T v) = 切片)` | 同上；可迭代值只求值一次（`{data, len}` 快照） |
| `foreach (T i = lo:hi)` | 半开区间 `[lo, hi)`；`lo >= hi` 零次，两个界各只求值一次 |
| `foreach (T _ = …)` / `(_, T v)` | 丢弃这个绑定（丢弃的绑定连加载都不生成） |
| `foreach (int _ = 0:n) { … }` | “重复 n 次”的写法 |
| `foreach (T v = it)` | 迭代器：`it` 是 `T`、`T *` 或接口值，类型提供 `int has(T *)` + `E next(T *)`；`v` 是 `next()` 返回值的副本 |
| 头里的类型 | 就是绑定在体里的类型：可以比元素宽或窄，按赋值规则转换 |
| 值绑定 `v` | 副本：写它不改容器 |
| 下标绑定 | 每轮从计数器拷的一份副本：写它不影响迭代 |
| `break` / `continue` / `defer` / `await` | 与写在 `for` 体里完全一致 |

规则：

- 可迭代对象有四种：**完整数组**（含字符串字面量）、**切片 `T[:]`**、
  **整数区间 `lo:hi`**，以及**迭代器协议**（`has()`/`next()`）。其它
  指针、整数、VLA、不完整数组、没有暴露切片的结构体都报错，并给出
  改法提示；
- 迭代器协议是**结构化**的：不看接口叫什么名字，只看两个方法的签名。
  `has()` 返回整数（非零继续），`next()` 返回元素 `E`（完整、可赋值，
  非数组/函数/`void`）；两个方法只接收 receiver，并且在 `foreach`
  之前可见。`T`（struct/union 值）走直接调用，`T *` 和接口值也可以；
  接口值通过 vtable 分派；
- 只求值一次：数组/切片落到隐藏的 `{元素指针, 长度}` 帧槽（与切片的
  运行期布局相同），区间上界落到隐藏整数槽，迭代器落到它自己的隐藏槽，
  因此循环体里改容器不影响迭代次数；
- 迭代器**只绑定一个值**：`foreach (T v = it) { … }`（`(T _ = it)` 也合法，
  仍调用 `next()` 推进）。值形态把迭代器状态复制到隐藏槽（循环不推进原
  对象），`T *` / 接口形态直接操作原对象（`next()` 的修改可见）；
- 头只有三种写法：`foreach (T v = a)`（只要值）、
  `foreach ((T i, T v) = a)`（下标 + 值）、`foreach (T i = lo:hi)`（区间
  下标）；绑定在头里声明，类型就是它在体里的类型。循环体**必须**是花括号
  块，绑定的作用域就是这条语句，`_` 表示丢弃；
- 迭代状态（计数器、切片对、迭代器）住在隐藏槽里，所以循环体里改容器、
  改下标都不影响迭代次数（下标是每轮拷出来的副本）；数组/切片最多两个
  绑定，区间和迭代器只有一个；
- 元素本身是数组时（`int[2][3]`、`Row[:]`）不能遍历：值绑定会是一个
  数组，而数组在 C 里不可赋值；改用下标区间
  `foreach (size_t i = 0:(int)rows.len) { … rows[i] … }`；
- 下标/值都是普通局部变量，所以可以在 `async` 函数体里用（隐藏槽随帧
  一起保存/恢复），也可以在 `defer` 体、嵌套循环里用；
- 旧的绑定表写法（`foreach |i, v| a`）已经删除：写成它就报
  `'foreach' needs a parenthesised head now`，按上面三种头改一行即可；
- `foreach` 仍然可以当普通标识符：`int foreach = 1;`、`foreach(x);`、
  `p->foreach`、`goto foreach;` 都照常工作——只有语句开头、下一个记号是
  `(`、并且 `foreach` 不是作用域里已知的标识符时，才按语句解析；
- 迭代器的 `next()` 返回数组、`void`、`void *`（元素类型会被擦除）或
  不完整类型都报错；`foreach ((T i, T v) = it)` 也报错（迭代器没有下标
  绑定）。要下标自己维护计数器；值形态的 `T` 想“advance 原对象”就写
  `foreach (T v = &it)`，`T *` 和接口值本来就直接操作原对象；
- 托管引用 `@T` 可以作迭代器或元素：隐藏槽按声明的托管局部量初始化，
  并在 `foreach` 作用域结束时释放，`@T` 参数则按借用拷贝；`struct`
  里含 `@T`/`weak @T`/`own @T` 字段时仍不能按值绑定（沿用 GC 的
  “用 `new` 分配、用 `@T` 持有”的限制）。

完整可运行示例见 [examples/foreach_demo.c6](../../examples/foreach_demo.c6) 与
[examples/foreach_iter_demo.c6](../../examples/foreach_iter_demo.c6)：

```sh
./tcc/c6 -B tcc -run examples/foreach_demo.c6
./tcc/c6 -B tcc -run examples/foreach_iter_demo.c6
```
