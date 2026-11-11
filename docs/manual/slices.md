# 切片 `T[:]`

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> 带长度与边界检查的数组视图，两个机器字 `{元素指针, 长度}`，越界由 `libtcc1` 的检查陷阱兜住。
>
> 相关：示例 `examples/slice_demo.c6`；用例 `tests/slice.c6`

---

## 切片 `T[:]`

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

完整可运行示例见 [examples/slice_demo.c6](../../examples/slice_demo.c6)：

```sh
./tcc/c6 -B tcc -run examples/slice_demo.c6
```

跨 TU 时两边各自写 `T[:]` 即可（布局固定），不需要额外声明。
