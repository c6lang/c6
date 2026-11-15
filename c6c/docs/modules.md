# c6c 的 module / import 设计（v0 草案）

目标：把上游 `tests/module.c6` + `tests/tu/module.c6`、`tests/err_ids.c6` +
`tests/tu/err_ids.c6` 跑通。这两个用例就是这条线的验收，现在的状态是
**只做了"限定引用的名字改写"**（`a.b.c` → `a__b__c`），定义侧没加前缀，
所以 `tests/module.c6:59` 报 `未声明的标识符 'tu__module__add'`。

## 1. 口径（从两个用例反推出来的规范）

1. **文件作用域的名字加模块前缀。** 有 `module tu.module;` 的文件里，源码写
   短名（`add`），导出符号是 `tu__module__add`。加前缀的对象：函数、全局变量、
   结构体/联合体/枚举的 tag、枚举常量、typedef、模板、方法
   （`Point_area` → `tu__module__Point_area`）、接口名。
2. **模块内部照样用短名。** 同一个文件里 `twice` 调 `add`，不用写限定名。
   所以定义时既要写带前缀的符号，也要留短名映射给本文件的引用。
3. **导入方可以用三种写法**：短名（`add(2,3)`）、全限定
   （`tu.module.add`、`tu.module.Point`）、前缀化 C 名
   （`tu__module__add`、`tu__module__counter`）。三种必须指向同一个对象。
4. **import 只取声明，代码来自定义它的那个翻译单元**。用例是两个 `.c6`
   分别编译再链接，`import` 不得把实现体再生成一遍（否则重复符号）。
5. **错误号段**：`id = (area << 27) | (slot << 12) | code`，bit31 恒 0。
   * 没有 module 的文件：slot = 0，`err(7,"…")` 的 id 就是 7。
   * 模块自己的号段：area = 0，slot 由编译器分配（同一模块恒定）。
   * `std.core.err` 内置：area = 1，slot = 0x6000。
   * `0` 永远是"没有错误"，不加基址。

## 2. 数据结构

```
/* 一个源文件属于哪个模块（pp 看到该文件的 `module a.b;` 时登记） */
static u32  FILE_MOD[SRC_MAX];      /* 0 = 不属于任何模块，否则是 MODS[] 下标 */
static Mod  MODS[MOD_MAX];
struct Mod {
    const char *path;      /* "tu.module"（点分，规范化后） */
    const char *prefix;    /* "tu__module__"（点换成 __，末尾带 __） */
    u32 slot;              /* 错误号段里的 slot，第一次见到时分配 */
    u32 sym_from;          /* 导出表起点（见下） */
};
```

导出表给"短名解析"用：定义模块成员时同时记 `(mod, 短名) → Sym*`，
导入方查不到裸名时按自己的 `import` 列表逐个查。

```
static u32 IMPORTS[IMP_MAX];        /* 当前 TU 导入的 MODS[] 下标，按导入顺序 */
```

## 3. 名字改写点

| 位置 | 现在 | 要改成 |
| --- | --- | --- |
| `sym_add()` 定义 | 原名 | 当前文件有模块就 `prefix + name`；同时登记短名映射 |
| 裸名查找 `sym_find()` | 只在当前 TU 表里找 | 失败后按 `IMPORTS` 逐个查导出表 |
| 限定引用 `a.b.c`（表达式/类型位置） | 直接拼 `a__b__c` | 先匹配 `import` 里的模块前缀，命中就拼 `prefix + c`；`a.b.C` 是类型时同理 |
| 方法 | `Point_area` | `prefix + "Point_area"` |
| `#include` 进来的普通头文件 | 无前缀 | 无模块 → 不加前缀（保持现状） |

## 4. 实现顺序（每一步都能单独验证）

1. **M1 模块登记**：pp 处理 `module a.b;` 时登记 `FILE_MOD[file]` 与
   `MODS[]`（path/prefix），`import a.b;` 记 `IMPORTS[]`。暂时不改名，
   只保证 `-v` 能打出来、现有用例不回归。
2. **M2 定义侧前缀**：`sym_add` 一条路径上按 `FILE_MOD` 加前缀，并登记短名
   映射；同文件内的裸名引用走短名映射。验证：`tests/tu/module.c6` 单编译
   出来的符号表里是 `tu__module__add`（`nm` 或 `--emit-c` 看）。
3. **M3 裸名解析**：导入方裸名查不到时按 `IMPORTS` 查导出表。
   验证：`tests/module.c6` 的 `add(2,3)`/`Point`/`RED`/`counter` 那几块。
4. **M4 限定名与取地址**：`tu.module.add`、`tu.module.Point`、
   `int (*f)(int,int) = tu.module.add;`。
5. **M5 错误号段**：`err(code,"…")` 与 `enum Error` 成员按
   `(area << 27) | (slot << 12) | code` 加基址，`std.core.err` 内置
   `(1, 0x6000)`，`0` 不加。验证：`tests/err_ids.c6`。
6. **M6 两 TU 链接**：`tests/tu/*.c6` 逐个编译成对象再用链接器合起来，
   确认没有重复符号、`import` 没有把实现体带进来。

## 5. 已知的坑

* `c6_rewrite_modules()`（src/src.c6:94）现在把 `import a.b.c;` 改写成
  `#include "a/b/c.c6"`，也就是**靠文本包含拿到声明**。这条路在 M2 之后
  仍然能用（定义在别的 TU），但要小心：同一份 `.c6` 被两个 TU 包含时，
  里面的 `module` 语句会把 `FILE_MOD` 改回它自己的模块 —— 这是对的，
  因为符号归属该看声明的那个文件，而不是包含它的那个文件。
* `static` 成员不该加前缀（内部链接），但 `tests/tu/*.c6` 里没有这种写法，
  M2 先统一加，遇到再分叉。
* 模板实例化出来的名字里禁止出现 `.`（`ty_name` 要能用），前缀用 `__`。
