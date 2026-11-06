# superc —— 带语言扩展的 TinyCC

superc 是 [TinyCC](https://github.com/TinyCC/tinycc) 的一个分支，在不破坏原有 C 语义的前提下
加入了一组系统编程友好的语言扩展：

| 扩展 | 作用 |
|---|---|
| `defer { ... }` | 作用域/函数退出时逆序执行清理代码 |
| `errdefer { ... }` | 仅在错误路径上执行的 `defer` |
| `err(code, "msg")` | 设置错误槽并就地展开清理，返回哨兵值 `-1` |
| `test "描述" { ... }` + `-test` | 顶层测试块与测试运行器 |
| `package NAME { ... }` / `import NAME;` | 单文件与跨 TU 的包与名字修饰（`NAME__sym`） |
| `interface` / `impl` | 胖指针（`{void *self; const VT *vt;}`）+ vtable 的接口抽象 |
| `#macro` + `__local` | 卫生宏：`__local` 标识符按展开序号重命名 |

扩展默认不启用，且是**编译期可裁剪**的：不定义 `TCC_EXT` 构建出的编译器与上游 TCC 行为完全一致。

## 目录结构

```
.
├── docs/plan.md      设计与实施方案（含实现状态）
├── patches/          superc 扩展补丁（相对上游固定基线）
├── scripts/          bootstrap.sh：生成并搭建 TCC 工作树
├── Makefile          便捷入口（bootstrap / build / test / clean）
└── tcc/              TCC 源码树（由 bootstrap.sh 生成，不纳入本仓库）
```

## 快速开始

依赖：`git`、`make`、C 编译器（gcc/clang）、`binutils`。

```sh
# 1. 拉取上游 TCC，检出固定基线并应用 superc 补丁
./scripts/bootstrap.sh          # 等价于 make bootstrap

# 2. 构建（打开扩展）
make build                      # 等价于 make -C tcc -j EXTRA-DEFS=-DTCC_EXT

# 3. 运行
./tcc/tcc -std=tcc-ext -B. -run hello.c
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
        printf("error %d: %s\n", __tcc_err_get_id(), __tcc_err_get_msg());
        errclear();
    }
    return 0;
}
```

`err();`（不带参数）用于在调用方处理后**重抛**错误。

### test 块

```c
test "add works" {
    if (add(1, 2) != 3)
        err(1, "1 + 2 != 3");
}
```

```sh
./tcc/tcc -std=tcc-ext -B. -test tests.c               # 运行全部 test 块
./tcc/tcc -std=tcc-ext -B. -test -test-filter=add tests.c
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
    Circle c; c.r = 2;
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

```sh
make test          # 扩展模式：tests/tests2 全量（含 156..162 的 superc 专项测试）
make test-plain    # 关闭 TCC_EXT 重建后再跑一遍，验证“扩展关闭零行为变化”
```

> 切换模式会先 `make clean` 重建，因为 `TCC_EXT` 是编译期开关。

## 实现说明

- 设计文档：[docs/plan.md](docs/plan.md)（第 11 节为各阶段实现状态与限制）。
- 运行时状态（defer 记录栈、错误槽）是普通全局变量：`tcc -run` 不支持 TLS，
  因此这一版是单线程语义。
- `interface` 的 `impl` 必须先于使用点可见（TCC 单趟编译的固有限制）。
- 跨 TU：头文件里用 `package NAME { ... }` 声明 API；`impl IFACE for TYPE;` 只声明，
  vtable 由另一个 TU 提供。

## 上游与许可

TCC 源码及本仓库的补丁均遵循 **LGPL-2.1**（见 `tcc/COPYING`）。
上游基线固定在 `2d3eea2`（`origin/mob`）。
