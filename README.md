# c6 —— 带语言扩展的 TinyCC

c6 是 [TinyCC](https://github.com/TinyCC/tinycc) 的一个分支，在不破坏原有 C 语义的前提下加入
一组系统编程友好的语言扩展。**开关只有两个**：编译器打开 `C6_EXT`、源文件用 `.c6` 后缀
（`.c` 文件永远是普通 C，没有 `-std=c6` 这个选项）。

| 扩展 | 作用 | 详情 |
|---|---|---|
| `defer` / `errdefer` | 作用域/函数退出时逆序清理；`errdefer` 只在错误路径执行（状态在函数帧里，不需要 TLS） | [错误处理](docs/manual/errors.md) |
| `T!error` + `try` / `catch` | 声明可能失败的函数；`try` 原样传播，`catch e { ... }` 就地处理并清空错误槽 | [错误处理](docs/manual/errors.md) |
| `return err(id, "msg")` | 设置错误槽、就地展开清理并直接返回（只能写在 `T!error` 函数里） | [错误处理](docs/manual/errors.md) |
| 错误码号段 | 每个模块一个号段（`(area<<27)\|(slot<<12)\|code`），`errbase.def` 钉号，撞号是硬错误 | [错误码](docs/manual/errors.md#错误码是模块自己的号段) |
| `module` / `import` | 一个文件一个模块，模块名就是它的路径；`mod.member` 限定访问，符号带 `mod__` 前缀 | [模块](docs/manual/modules.md) |
| 结构体方法 | `ret f(struct T *self, ...)` 即 `T` 的方法，`p->f()` / `v.f()` 都是调用（符号是 `T_f`） | [方法与 interface](docs/manual/methods.md) |
| `interface` | 胖指针 `{void *self; const VT *vt;}` + 自动生成的 vtable | [方法与 interface](docs/manual/methods.md) |
| 省略 `struct` | tag 本身就是类型名：`Vec v;`、`sizeof(Vec)`、`Vec[:]` | [声明](docs/manual/declarations.md) |
| 变量必须初始化 | 每个变量定义都带初始化器（`static` 与 VLA 除外） | [声明](docs/manual/declarations.md) |
| `T[:]` 切片 | 带长度与边界检查的数组视图，两个机器字 `{元素指针, 长度}` | [切片](docs/manual/slices.md) |
| `foreach` | 遍历数组、切片、整数区间，以及实现 `has()`/`next()` 的迭代器 | [foreach](docs/manual/foreach.md) |
| 元组 `(T1, T2, ...)` | 函数多返回值：`return {q, r};`、`(int q, int r) = f();`、`t[0]` | [元组](docs/manual/tuples.md) |
| 函数字面量 / 闭包 | `void(int x) { ... }` 是表达式；`closure RET (P…)` 按值捕获外层块作用域 | [函数字面量](docs/manual/functions.md) |
| `async` / `await` / `co` / `task` | 无栈协程：`co f();` 顶层跑，`task t = f();` 拿句柄 | [并发](docs/manual/concurrency.md) |
| `spawn` | OS 线程（`pthread_create`），语句立刻返回、真并发 | [并发](docs/manual/concurrency.md) |
| 托管引用 `@T` | 引用计数 GC：`@T` / `weak` / `own` / `shared` / `drop`，环是硬错误 | [托管引用](docs/manual/gc.md) |
| `f"..{expr}.."` | 零分配栈缓冲的字符串插值；`@f".."` 产出托管字符串 | [字符串插值](docs/manual/fstrings.md) |
| 宏 `expr`/`stmt`/`ident`/`type` | 像函数一样写宏，角色词说明展开结果怎么用，局部变量自动卫生重命名 | [宏](docs/manual/macros.md) |
| 泛型 `@` | `struct Vect@T { ... }` 定义模板，`Vect@int` 建实例；`\|T\|` 前缀绑定形参；单态化导出普通 C | [泛型设计](docs/design/generics-design.md) |
| 无 libc | freestanding：运行库外部依赖全是 weak，一直到 512 字节引导扇区的裸机 | [无 libc](docs/manual/freestanding.md) |
| `test "描述" { ... }` + `-test` | 顶层测试块与内建测试运行器 | [测试](docs/manual/testing.md) |

扩展默认不启用，且是**编译期可裁剪**的：不定义 `C6_EXT` 构建出的编译器与上游 TCC 行为完全一致。
`C6_EXT` 只决定「有没有这套扩展」，真正启用哪一份代码由**源文件后缀**决定：`.c6` 文件开启扩展
（并定义 `__C6__`），`.c` 文件不会因为同一次编译里出现 `.c6` 而被连带开启。`import NAME;`
优先查找 `NAME.c6`，找不到才回退到 `NAME.c`。

## 快速开始

依赖：`git`、`make`、C 编译器（gcc/clang）、`binutils`。

```sh
# 1. 配置（只有第一次需要；tcc/ 源码就在仓库里，不用联网拉取）
./scripts/bootstrap.sh          # 等价于 make bootstrap

# 2. 构建（打开扩展；会先自动执行 bootstrap）
make build                      # 等价于 make -C tcc -j EXTRA-DEFS=-DC6_EXT

# 3. 运行（-B tcc 让编译器找到未安装的 libtcc1.a 与运行时头文件）
./tcc/c6 -B tcc -run hello.c6   # c6 代码放在 .c6 文件里，后缀就是开关

# 4. 提交前验证：一条命令跑完整回归（最慢的一条，见「构建与验证」）
make check-all
```

`make build` / `make selftest` / `make test-all` / `make check-all` 都会先跑 `bootstrap`：
`tcc/` 已经配置过（有 `config.mak`）就直接跳过，没有才跑一次 `./configure`。

`tcc/` 是普通源码：上游 TinyCC 的基线提交 2d3eea2 先整体导入，之后每个 c6 补丁一个提交
（提交说明就是补丁标题）。要改 TCC 就直接改 `tcc/` 里的文件、按普通提交提交——没有
“重新同步工作树”这回事，手改不会被覆盖。`make distclean` 清理构建产物和 configure 结果，
不会删源码。

## 目录结构

```
.
├── README.md           本文件：落地页与总索引
├── CHANGELOG.md        版本与特性变更史
├── docs/               文档（入口：docs/README.md）
│   ├── manual/         语言手册：一个特性一篇，讲怎么用
│   ├── reference/      规范与参考：语法、诊断码、命令行、后端
│   ├── design/         设计文档与实现说明：讲为什么这么做
│   ├── history/        实施记录与状态：讲做到哪一步了
│   └── stdlib.md       标准库 std/ 的设计、API 与写法约定
├── scripts/            bootstrap.sh，以及裸机/wasm 辅助脚本
├── std/                标准库：一个文件一个 module（std/vec.c6 -> std.vec）
├── tests/              c6 全部自测：test 块、编译期诊断、上游 tests2
├── examples/           可运行示例：每个特性一个 *_demo.c6，wordcount 是集成示例
├── Makefile            构建入口（make help 看全部目标）
└── tcc/                TCC 源码树：上游基线 + 每个 c6 补丁一个提交
```

## 文档地图

| 你是…… | 从这里读 |
|---|---|
| 第一次接触 c6 | 上面的[快速开始](#快速开始) → [语言手册](docs/manual/) 挑要用的特性 |
| 写 c6 代码 | [docs/manual/](docs/manual/)（语法与陷阱）+ [docs/stdlib.md](docs/stdlib.md)（标准库 API） |
| 查语法规范 | [docs/reference/grammar.md](docs/reference/grammar.md)（EBNF 增量语法、静态约束、重写规则） |
| 查报错信息 | [docs/reference/diagnostics.md](docs/reference/diagnostics.md)（C1–C134 约束与诊断文本） |
| 查命令行/构建目标 | [docs/reference/cli.md](docs/reference/cli.md)（编译器选项 + 全部 make 目标） |
| 改编译器 | [docs/design/plan.md](docs/design/plan.md) → [docs/design/implementation-notes.md](docs/design/implementation-notes.md) → [grammar.md §11](docs/reference/grammar.md#11-实现位置)（实现位置表） |
| 看实现进度 | [docs/history/status.md](docs/history/status.md)（分阶段状态）+ [CHANGELOG.md](CHANGELOG.md) |
| 后端（C99 / LLVM） | [docs/reference/c99-backend.md](docs/reference/c99-backend.md)、[docs/reference/llvm-backend.md](docs/reference/llvm-backend.md) |
| 裸机 / freestanding | [docs/manual/freestanding.md](docs/manual/freestanding.md) |

完整索引（按读者路径分三条）见 [docs/README.md](docs/README.md)。

## 构建与验证

```sh
make build        # 扩展模式（EXTRA-DEFS=-DC6_EXT）
make build-plain  # 上游模式（不定义 C6_EXT）
make test         # tests2 上游全量（扩展模式）
make test-all     # selftest + tests2 + c6selfcheck + 标准库 + freestanding/裸机
make check-all    # 提交/发布前跑这一条：test-all + errcheck + C99 后端 + 三条自举路线
```

每个目标的含义、自举三条路线怎么比对、`make help` 的完整目标表，见
[docs/reference/cli.md](docs/reference/cli.md)；测试怎么组织、诊断用例怎么写，见
[docs/manual/testing.md](docs/manual/testing.md)。

## 标准库 `std/`

标准库按「一个文件一个 module、模块名就是它的路径」组织：

```
std/types.c6   std.types   定宽类型 u8/i32/u64…、usize、f32/f64、Limits/Bool
std/err.c6     std.err     错误码 enum Error（OK…BUSY）+ msg()/msg_of()/is_ok()
std/vec.c6     std.vec     会长的数组 Vect@T（托管引用）+ Iter@T：push/pop、insert/remove_at、index_of、slice/extend、sort/each/filter/fold、any/all/find_if/swap
std/span.c6    std.span    别人内存上的窗口 Span@T（纯值、零拷贝）
std/map.c6     std.map     开放寻址哈希表 Map@(K,V) + Entry/Iter：put/get/get_or/set/put_new/remove
std/str.c6     std.str     字符串视图 Str + 零成本字面量宏 S()：split/lines/join、find/last_find、trim、to_int/to_u64、strip_prefix/suffix、fields、replace/split_once、hash
std/fmt.c6     std.fmt     定长格式化缓冲 Buf@(N)：write_int/u64/hex/hex64/bin/bool/ptr、write_pad（共用 digits_* 原语）
std/range.c6   std.range   能被 foreach 吃掉的区间 Range/StepRange
std/sort.c6    std.sort    比较器驱动的排序与二分查找：sort_int/sort_bytes、find_int、lower/upper_bound
std/mem.c6     std.mem     借用式内存池 Arena、托管堆包装 alloc_new/dealloc、按元素的 copy/fill/swap/rotate/align
std/io.c6      std.io      Reader/Writer（文件与标准流）：read_line/read_all、write_line/flush、零分配 CFile 迭代
std/math.c6    std.math    整数 gcd/lcm/ipow/clamp/floor_div/mod + 浮点 sqrt/floor/ceil/round（不链 libm）
std/time.c6    std.time    now_ns/now_ms/now_unix、sleep_ms、Duration 格式化、UTC 日历换算（不依赖 localtime）
std/strbuf.c6  std.strbuf  会长的字符缓冲 StrBuf（托管引用）：write/write_int/write_hex、reserve/truncate/cstr
std/utf8.c6    std.utf8   UTF-8 解码/编码/校验/计数 + Rune 迭代器（拒绝过长编码、代理区、截断）
std/bits.c6    std.bits   popcount/clz/ctz、rotl/rotr、bswap、bit_get/set/clear、mask/sign_extend/is_pow2
std/hash.c6    std.hash   FNV-1a 32/64、CRC32、Adler-32 + 流式 Hasher
std/path.c6    std.path   POSIX 路径拆装：base/dir/ext/stem、join/normalize（写调用方缓冲，不分配）
std/deque.c6   std.deque  环形缓冲双端队列 Deque@T + DIter：push/pop 两端 O(1)、first/last/at/get/set
std/ascii.c6   std.ascii  ASCII 判别/大小写/忽略大小写比较（收 int，EOF 与负 char 安全）
std/num.c6     std.num    浮点格式化（固定/%e/%g）与解析（inf/nan、RANGE/PARSE 错误码）
std/codec.c6   std.codec  Base64、十六进制、百分号编解码（写调用方缓冲，放不下一个字节都不写）
std/rand.c6    std.rand   xorshift64*：可重放序列、无偏区间 rand_below、rand_f64、shuffle
std/fs.c6      std.fs     stat/lstat、mkdir_p、目录遍历 Dir、rename/remove、getcwd/chdir
std/set.c6     std.set    哈希集合 Set@K + SetIter（std.map 的薄包装，add 返回新增/已存在）
```

```c
import std.vec;
import std.fmt;

@Vect@int v = new Vect@int;                /* 托管字段 -> new 出来 */
Buf@(32) b = { { 0 }, 0 };                 /* 纯值，栈上就够 */

v->push(42) catch e { return e.id; };      /* 失败必须被 try/catch 消费 */
b->write_int(v->count()) catch e { return 2; };
printf("%s\n", b->cstr());
```

`import` 只带来声明，所以模块自己的 `.c6` 文件要一起编进程序（`std/vec.c6`
的排序内核在 `std/sort.c6` 里，sort 又依赖 str/err/types，用 vec 就得把这几个
正文都放上命令行）：

```sh
./tcc/c6 -B tcc -I . -o build/app app.c6 \
    std/vec.c6 std/fmt.c6 std/types.c6 std/str.c6 std/sort.c6 std/err.c6
make stdtest     # 跑 std/ 下全部 test 块（模块数与块数以实际输出为准）
make std-demo    # 编译并运行 examples/wordcount.c6（用到 8 个模块）
make std-tour    # 编译并运行 examples/stdtour.c6（第二批模块的拼装示例）
make std-kit     # 编译并运行 examples/stdkit.c6（第三批模块的拼装示例）
```

设计原则、每个模块的 API、写法约定和编译器层面的已知限制都写在
[`docs/stdlib.md`](docs/stdlib.md)。

## 实现说明

- **设计与动机**：[docs/design/plan.md](docs/design/plan.md)（分阶段方案与评审结论）。
- **运行时、ABI 与实现位置**：[docs/design/implementation-notes.md](docs/design/implementation-notes.md)
  （每个语法对应哪个实现函数，语法见 [docs/reference/grammar.md](docs/reference/grammar.md)）。
- **阶段与验收状态**：[docs/history/status.md](docs/history/status.md)；**变更史**：[CHANGELOG.md](CHANGELOG.md)。
- 三条要点：运行时状态（错误槽、插值构造器、协程池、子线程链表）是**每线程**一份，用
  `pthread_key` 而不是 `__thread`（`tcc -run` 不支持 TLS），`defer` 的清理状态不在其中——
  它在函数帧里；方法必须在调用点之前声明或定义（TCC 单趟编译的固有限制）；元组复用
  `VT_STRUCT`、切片是普通对象，两者的 ABI 都不随 `#pragma pack` 改变。

## 上游与许可

TCC 源码及本仓库对它的修改均遵循 **LGPL-2.1**（见 `tcc/COPYING`）。
上游基线固定在 `2d3eea2`（`origin/mob`）。
