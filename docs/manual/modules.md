# 模块：module / import

> **状态**：现行规范（`C6_EXT` + `.c6` 后缀） · **索引**：[文档索引](../README.md) · **实现状态**：[status.md](../history/status.md)
>
> 一个文件一个模块，模块名就是它的路径；`mod.member` 与 `mod__sym` 前缀由这一条决定。
>
> 相关：用例 `tests/module.c6`、`tests/tu/module.c6`

---

## module / import（模块名就是它的路径）

`module` 是唯一的命名空间：一个文件一个模块，模块名就是它在磁盘上的路径。

```c
/* geo/shape.c6 */
module geo.shape;

struct Point { int x, y; };
typedef struct Point Point;
int add(int a, int b) { return a + b; }
int area(Point *p) { return p->x * p->y; }

/* app.c6：import 按 'geo/shape.c6' 这条路径找到它 */
import geo.shape;

int main(void)
{
    Point p = { 3, 4 };
    return add(1, 2) == 3 && area(&p) == 12 ? 0 : 1;
}
```

规则：

- `module NAME;` 必须是文件的第一个声明，一个文件只能声明一次；`main` 不能声明
  在模块里；
- **模块名就是路径**：`import a.b;` 只按 `a/b.c6`（或 `a/b.c`）查找，顺序是
  「当前文件目录 → `-I` 路径 → 当前目录」；找到的文件必须逐段声明成
  `module a.b;`，否则报 `module file declares 'a__zzz', expected 'a__b'`；
- 扁平的 `a.b.c6` / `a__b.c6` **不接受**：它存在时报
  `module 'a.b' must live at 'a/b.c6', not in a flat 'a.b.c6'`。分级路径先在
  每个搜索根里找完，扁平文件才用来报错，所以正确摆放的模块不会被旁边的散落
  文件干扰；
- 找不到模块文件是错误：`cannot find module 'a.b': expected a file at
  'a/b.c6' in the search path`；
- `module NAME;` 把文件作用域的声明改成 `NAME__sym`（点号名 `geo.shape` 的前缀是
  `geo__shape__`），块作用域、`static`、`test`、`__` 开头的名字和 `#include` 进来
  的普通 C 文件不受影响；
- 类型也能写全限定名：`geo.shape.Point p;` 解析到 `geo__shape__Point`（模块里的
  typedef 或 struct/union/enum tag 都行，多级名字同理）；只有当前模块或 `import`
  过的名字可以这样写；
- 方法照旧按接收者取名：`int area(Point *p)` 导出 `geo__shape__Point_area`；模块
  文件**不**为方法发原名弱别名，C 侧只看到前缀符号；
- 成员访问与限定名可以混写：`mod.origin.x`、`*&mod.origin.x` 把尾部的 `.x` 交回
  普通成员访问，所以模块里的全局结构体照常逐字段使用；局部变量或字段与模块同名时
  字段优先；未声明的成员报 `'mod.nope' undeclared`；
- `import` 必须出现在使用点之前，循环 import 报 `circular import of module 'x'`；
- 导入只带**声明**：函数只取原型、跳过函数体，全局变量只取 `extern`、跳过初始化器。
  代码仍在模块自己的目标文件里，链接时把它（或库）放上命令行；
- `-emit-header FILE`（配合 `-c`）可把模块导出写成普通 C 头文件：
  `tcc -B tcc -c -emit-header geo/shape.h geo/shape.c6`。头文件里是
  `struct geo__shape__Point`、`geo__shape__add`、`geo__shape__Point_area` 这些
  真正的 C 名；切片和元组会生成 `geo__shape__slice_N` / `geo__shape__tuple_N`
  typedef。纯 C 一侧 `#include` 就能调用，不需要知道模块的存在；
- 直接编译一个模块文件时**不**检查它放在哪里：路径约束是给消费者（`import`）用的，
  单元测试把文件放在任意位置单独编译仍然成立。

`package`（块形式 `package X { ... }` 与文件级 `package X;`）已经删除：它和
`module` 是同一件事的两套写法，两套前缀规则会漂移。旧代码里的 `package` 仍被
识别，但立刻报 `'package' was removed: write 'module NAME;' as the first
declaration of the file`。迁移方式：把 `package a.b { ... }` 的内容挪进
`a/b.c6`，首行写 `module a.b;`。
