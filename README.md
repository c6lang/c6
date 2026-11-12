# c6 —— 带语言扩展的 TinyCC

## 先看一眼：`defer` / `errdefer` 管资源

C 里最容易写错的不是功能，而是**每条出口都得把已拿到的资源还回去**。c6 让你在拿到资源的
那一行就把它登记给 `defer`，函数出口只剩一处；「失败才需要撤销」的那部分单独交给 `errdefer`：

```c
/* 以前：出口有几条，清理就写几遍；再添一个资源，所有出口都得重排 */
int save(const char *path, const char *data, int n)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    char *tmp = malloc((size_t)n);
    if (!tmp) { fclose(f); return -1; }
    if (fwrite(data, 1, (size_t)n, f) != (size_t)n) { free(tmp); fclose(f); remove(path); return -1; }
    free(tmp); fclose(f);
    return 0;
}
```

```c
/* c6：拿到就登记，清理只写一次；出错时的回滚写在 errdefer 里 */
int!error save(const char *path, const char *data, int n)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return err(1, "open failed");
    errdefer remove(path);                 /* 只有失败才删半成品 */
    defer fclose(f);                       /* 正常返回、err 返回都执行 */

    char *tmp = malloc((size_t)n);
    if (!tmp)
        return err(2, "out of memory");
    defer free(tmp);                       /* LIFO：free 先于 fclose */

    if (fwrite(data, 1, (size_t)n, f) != (size_t)n)
        return err(3, "short write");      /* 跑 free → fclose → remove */
    return 0;
}
```

记住三条：登记顺序的**倒序**执行（`defer` 与 `errdefer` 混在一起排队）；`defer` 覆盖所有出口
（`return` / `break` / `continue` / `goto` / `err`）；`errdefer` 只在本次调用真的 `err` 过时跑，
正常返回跳过。清理状态放在**函数帧**里而不是 TLS，所以 `tcc -run` 和裸机上一样能用。
细节见[错误处理](docs/manual/errors.md)，可运行示例 `./tcc/c6 -B tcc -run examples/err_demo.c6`。

## 这是什么

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
├── std/                标准库：一个文件一个 module（std/container/vec.c6 -> std.container.vec）
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

标准库按「一个文件一个 module、模块名就是它的路径」组织，再按领域分七个子目录
（目录是模块名的一部分：`std/container/vec.c6` 就是 `std.container.vec`）：

```
std/core/                   基础：定宽类型、错误码、内存、数学、位运算、哈希、随机、原子操作、向量/统计/矩阵/插值
  std/core/atomic.c6        std.core.atomic        原子操作与自旋锁（零分配、不依赖 pthread）：Atomic(i64)/Atomic32(int) 的 load/store/add/sub/fetch_*/exchange/cas/位运算，默认 seq_cst（另有 relaxed/release 发布口）、fence 屏障、TAS 自旋锁 SpinLock（不变量：字段 v 只通过方法访问）
  std/core/bignum.c6        std.core.bignum        任意精度整数（1024 位、零分配）：big_parse/parse_base/from_i64/from_u64、Big 的 add/sub/mul/div/mod/divmod/pow、cmp、to_i64/to_u64、format/format_base（2..36 进制）；超 1024 位报 err
  std/core/bits.c6          std.core.bits          popcount/clz/ctz、rotl/rotr、bswap、bit_get/set/clear、mask/sign_extend/is_pow2
  std/core/complex.c6       std.core.complex       复数 Complex（f64 实部/虚部）：cplx/cplx_real/cplx_i、add/sub/mul/div/neg/conj/inv/scale/normalize、abs2/abs（用 std.core.math 的 sqrt）、pow_int、eq/near/is_finite
  std/core/decimal.c6       std.core.decimal       十进制定点 Dec（9 位小数、i64）：dec_parse/from_i64/from_f64/from_units、add/sub/mul/mul_int/div（128 位中间值、精确舍入）、round（TRUNC/HALF_UP/HALF_EVEN/FLOOR/CEIL）、to_i64/to_f64、format（恰好 N 位小数）
  std/core/crc.c6           std.core.crc           嵌入式校验：CRC-8（SMBUS/MAXIM）、CRC-16（CCITT-FALSE/XMODEM/MODBUS/ARC）、CRC-32C，逐位不带表；Fletcher-16/LRC/XOR8/sum8/sum16 与流式 crc*_update
  std/core/err.c6           std.core.err           错误码 enum Error（OK…BUSY）+ msg()/msg_of()/is_ok()
  std/core/fixed.c6         std.core.fixed         Q16.16 定点 Fix（from_int/from_frac/from_f64、to_f64/to_int/round/floor/ceil/frac、mul/div/sqrt/sin/cos/lerp/clamp、饱和加减）与 Q15（q15_from_*、mul/add_sat/sub_sat）
  std/core/hash.c6          std.core.hash          FNV-1a 32/64、CRC32、Adler-32 + 流式 Hasher
  std/core/math.c6          std.core.math          整数 gcd/lcm/ipow/clamp/floor_div/mod + 浮点 sqrt/floor/ceil/round（不链 libm）
  std/core/mem.c6           std.core.mem           借用式内存池 Arena、托管堆包装 alloc_new/dealloc、按元素的 copy/fill/swap/rotate/align
  std/core/pool.c6          std.core.pool          定长块内存池 Pool：pool_make/init、O(1) 的 alloc/free、contains/count/capacity/used/block_size/is_empty/is_full/reset（借用调用方缓冲，不碰堆）
  std/core/rand.c6          std.core.rand          xorshift64*：可重放序列、无偏区间 below/int_range、next_f64/next_bool、fill/shuffle
  std/core/rational.c6      std.core.rational      精确有理数 Rat（i64 分子/分母、自动约分）：rat_make/from_i64/parse、add/sub/mul/div/inv/neg/abs、cmp（连分数比较，不经过 double）、to_i64/to_f64、format（n/d）
  std/core/types.c6         std.core.types         定宽类型 u8/i32/u64…、usize、f32/f64、Limits/Bool
  std/core/geometry.c6      std.core.geometry      Vec2/Vec3/Rect：vec2_*/vec3_* 的 add/sub/scale/dot/cross/len/norm/lerp、rect/rect_contains/rect_intersect/rect_union/rect_expand、clampf/deg2rad/rad2deg（牛顿迭代开方）
  std/core/interp.c6        std.core.interp        插值与缓动：lerp/lerp_clamped/inv_lerp/remap/smoothstep/smootherstep、ease(kind,t) 共 11 种曲线、catmull_rom
  std/core/stats.c6         std.core.stats         Welford 流式统计（add/mean/variance/variance_pop/stddev/min/max）、stats_median/quantile/histogram/corr
  std/core/matrix.c6        std.core.matrix        行主序矩阵：mat/mat_strided、at/set/fill/identity/add/sub/scale/mul/transpose/trace、det/solve（部分主元高斯消元，scratch 由调用方给）

std/text/                   文本：字符串视图与缓冲、格式化、UTF-8、ASCII、浮点、glob、显示宽度、表格与自然排序
  std/text/ascii.c6         std.text.ascii         ASCII 判别/大小写/忽略大小写比较（收 int，EOF 与负 char 安全）
  std/text/fmt.c6           std.text.fmt           定长格式化缓冲 Buf@(N)：write_int/u64/hex/hex64/bin/bool/ptr、write_pad（共用 digits_* 原语）
  std/text/num.c6           std.text.num           浮点格式化（固定/%e/%g）与解析（inf/nan、RANGE/PARSE 错误码）
  std/text/str.c6           std.text.str           字符串视图 Str + 零成本字面量宏 S()：split/lines/join、find/last_find、trim、to_int/to_u64、strip_prefix/suffix、fields、replace/split_once、hash
  std/text/strbuf.c6        std.text.strbuf        会长的字符缓冲 StrBuf（托管引用）：write/write_int/write_hex、reserve/truncate/cstr
  std/text/template.c6      std.text.template      `{{name}}` 文本模板（零分配）：tpl 工厂、Template 的 render/render_len/render_opt/vars；名字两侧空白忽略、`{{{{` 输出字面量 `{{`、缺变量严格报错（宽松模式给空串）、放不下不写半截
  std/text/utf8.c6          std.text.utf8          UTF-8 解码/编码/校验/计数 + Rune 迭代器（拒绝过长编码、代理区、截断）
  std/text/glob.c6          std.text.glob          shell 风格 glob（零分配）：Glob 的 match/match_path/has_magic、* ? [] 字符类与转义
  std/text/semver.c6        std.text.semver        语义化版本 2.0.0：semver_parse、Semver 的 compare/is_prerelease/format（严格前导零与标识校验）
  std/text/utf16.c6         std.text.utf16         UTF-16 解码/编码/计数：代理对、BMP、非法代理与截断（零分配）
  std/text/ansi.c6          std.text.ansi          ANSI 转义构造器 Ansi：SGR/基础与 256 色/清屏/光标移动/隐藏光标
  std/text/natural.c6       std.text.natural       自然排序与人类可读化：natural_cmp/natural_cmp_ci（数字段按整数比）、humanize_bytes/parse_bytes（KiB/MiB/GiB/TiB）、humanize_duration/parse_duration（1h 2m 3s / 500ms）、humanize_int（千分位）
  std/text/table.c6         std.text.table         对齐表格：table_make、Table 的 border/sep/pad 与 render/table_len，TBL_LEFT/RIGHT/CENTER；列宽按显示列数（CJK 2 列）
  std/text/width.c6         std.text.width         终端显示宽度（Unicode 15.0）：text_width/checked、按列 text_prefix_len/fit/pad/clip
  std/text/regex.c6         std.text.regex         回溯式正则（UTF-8 按码点）：re_compile/flags、Regex 的 find/find_from/match_at/matches/full/find_all、Match 的 span/group；字符类、分组与分支、* + ? {m,n}（含懒惰）、\d\w\s\b、锚点
  std/text/diff.c6          std.text.diff          行级差异与 unified 输出：diff_make（Hirschberg 线性空间 LCS、工作区走 Arena）、Diff 的 write_script/write_unified/script_len/unified_len/op_at 与 added/removed/same
  std/text/fuzzy.c6         std.text.fuzzy         模糊匹配：lev_distance/lev_distance_max/lev_ratio、damerau_distance（相邻换位算 1）、subsequence_match/subsequence_score、fuzzy_best（did you mean；按码点、256 上限）
  std/text/wrap.c6          std.text.wrap          按显示列折行/缩进/居中：wrap_make/wrap_indented、Wrap 的 count/write/lines、text_indent/text_center；CJK 2 列、超长词按码点硬切、放不下不写半截

std/container/              容器与算法：向量、位集合、跨度、映射、集合、双端队列、优先队列、区间、排序、图、LRU、前缀树、通道
  std/container/deque.c6    std.container.deque    环形缓冲双端队列 Deque@T + DIter：push/pop 两端 O(1)、first/last/at/get/set
  std/container/chan.c6     std.container.chan     有界通道（缓冲由调用方给）：chan_init(buf/cap/esize)、send/recv（满/空阻塞）、try_send/try_recv、send_ms/recv_ms（限时 1/0/-1）、close_chan（幂等）、count/capacity/is_empty/is_full/is_closed/sent_count/recv_count；元素按字节块拷贝
  std/container/map.c6      std.container.map      开放寻址哈希表 Map@(K,V) + Entry/Iter：put/get/get_or/set/put_new/remove
  std/container/pqueue.c6   std.container.pqueue   二叉堆 PQueue@T（托管容器）：push/pop/peek/push_pop/heapify/clear、count/capacity/is_empty/is_valid、init/set_cmp/set_max（比较器与 std.container.sort 的 cmp_int/cmp_str 通用）
  std/container/range.c6    std.container.range    能被 foreach 吃掉的区间 Range/StepRange
  std/container/set.c6      std.container.set      哈希集合 Set@K + SetIter（std.container.map 的薄包装，add 返回新增/已存在）
  std/container/sort.c6     std.container.sort     比较器驱动的排序与二分查找：sort_int/sort_bytes、find_int、lower/upper_bound
  std/container/bitset.c6   std.container.bitset   借用 u64 缓冲的位集合 Bitset：set/clear/toggle/bit/count/next_set、集合运算
  std/container/list.c6     std.container.list     侵入式双向链表 List/ListNode：push/pop/remove/clear、contains、ListIter
  std/container/ring.c6     std.container.ring     借用调用方数组的固定容量环形队列 Ring：push/pop/peek/at、is_full
  std/container/graph.c6    std.container.graph    邻接表图（借用调用方数组）：graph_init/add_edge、Graph 的 bfs/dfs/toposort/components/dijkstra（BFS 无权最短路、迭代 DFS、Kahn 拓扑排序、并查集弱连通分量、非负权 Dijkstra + 前驱链）
  std/container/lru.c6      std.container.lru      LRU 缓存（借用调用方数组，O(1)）：lru_init、Lru 的 get/peek/put/remove/clear/size/capacity/is_empty，命中/未命中/淘汰统计
  std/container/region.c6   std.container.region   半开区间集合（借用调用方数组）：region_init、Region 的 add（重叠与相邻合并）/contains/overlaps/find_gap/total/count_region/clear
  std/container/trie.c6     std.container.trie     字节前缀树（调用方节点池）：trie_init、Trie 的 put/get/has_prefix/remove/clear/size/node_count
  std/container/span.c6     std.container.span     别人内存上的窗口 Span@T（纯值、零拷贝）
  std/container/vec.c6      std.container.vec      会长的数组 Vect@T（托管引用）+ Iter@T：push/pop、insert/remove_at、index_of、slice/extend、sort/each/filter/fold、any/all/find_if/swap

std/os/                     操作系统：流、文件系统与目录树、路径、环境变量、时间、命令行参数、子进程、同步原语、线程池、终端、信号、管道、多路复用、临时文件、文件锁、内存映射
  std/os/env.c6             std.os.env             环境变量与命令行：env_get/set/unset、env_get_int/bool、env_args/env_arg
  std/os/fs.c6              std.os.fs              stat/lstat、mkdir_p、目录遍历 Dir、rename/remove、getcwd/chdir
  std/os/io.c6              std.os.io              Reader/Writer（文件与标准流）：read_line/read_all、write_line/flush、零分配 CFile 迭代，方法调用写 pr->read_line(...)
  std/os/path.c6            std.os.path            POSIX 路径拆装：base/dir/ext/stem、join/normalize（写调用方缓冲，不分配）
  std/os/args.c6            std.os.args            命令行选项解析（零分配）：args_open + Args.next(specs, n, &arg)、ArgSpec（长名/短名/是否带值）、Arg 的 kind/spec/name/value/index；--name=value、--name value、-n value、-- 终止、未知选项/缺值报错
  std/os/proc.c6            std.os.proc            子进程（POSIX）：proc_run/proc_spawn/proc_wait（ProcStatus 的 exited/exit_code/signaled/signal）、proc_run_sh（/bin/sh -c）、proc_which（PATH 查找）
  std/os/sync.c6            std.os.sync            阻塞式同步原语（pthread 薄封装、值类型）：Mutex 的 init/destroy/lock/try_lock/unlock、Cond 的 wait/wait_ms/signal/broadcast、RWLock 的 rdlock/wrlock/try_*、Once 的 call(fn)、Semaphore 的 init/wait/try_wait/wait_ms/post/value、Barrier 的 init/wait（返回 1 的是串行线程）
  std/os/thread.c6          std.os.thread          线程身份与线程池：thread_self/thread_equal、cpu_count、thread_yield、thread_set_name；ThreadPool 的 start(workers, jobs, cap)/submit（满时背压）/try_submit/wait/stop（join，幂等）、pending/running/done/workers/is_started；worker 是裸 pthread，任务 void (*fn)(void *)
  std/os/tty.c6             std.os.tty             终端（termios 薄封装）：is_tty、size/width/height（TIOCGWINSZ）、set_size（TIOCSWINSZ）、TtyMode 的 raw(fd)/restore/is_active（对非终端 raw 返回 0；恢复幂等）
  std/os/signal.c6          std.os.signal          信号（sigaction/sigprocmask 薄封装）：install（SA_RESTART）、ignore/reset、block/unblock/is_blocked/is_pending、send_self（raise）/send_to（kill）、signal_name/signal_number（带不带 SIG 前缀都认）
  std/os/pipe.c6            std.os.pipe            匿名管道（零分配）：init/init_nonblock、read_bytes（0=EOF）/read_exact、write_bytes（返回实际字节数）、set_nonblock、close_read/close_write/close_all、would_block；重试 EINTR、fd 带 FD_CLOEXEC
  std/os/poll.c6            std.os.poll            多路等待（poll(2)）：PollSet（32 槽、零分配）的 add/modify/remove_fd/find/count/wait（返回就绪个数）/is_ready/ready/fd_at，单 fd 的 wait_readable/wait_writable/wait_any（1/0/-1）；EVENT_READ/WRITE/HUP/ERROR
  std/os/time.c6            std.os.time            now_ns/now_ms/now_unix、sleep_ms、Duration 格式化、UTC 日历换算（不依赖 localtime）
  std/os/walk.c6            std.os.walk            目录树遍历与删除：walk/walk_stat（WalkOpts 的 max_depth/follow_links/visit_dirs、WalkStats 计数）、回调 1 继续 / 0 跳过子树 / -1 中止、remove_tree 后序删除
  std/os/sysinfo.c6         std.os.sysinfo         uname 五字段（静态缓冲区视图）、运行时长、负载、CPU 数、页大小
  std/os/resource.c6        std.os.resource        rlimit 读写（8 种限制，-1 表示无限）、getrusage（毫秒 CPU 时间/峰值 RSS/缺页/上下文切换）、nice
  std/os/user.c6            std.os.user            passwd/group 查询：user_by_uid/name/current、group_by_gid（查不到 0、出错 -1）
  std/os/pty.c6             std.os.pty             伪终端：pty_open 一对 fd、pty_spawn 起子进程、带 poll 超时的收发、pty_resize、pty_reap
  std/os/temp.c6            std.os.temp            临时文件/目录：temp_file/temp_file_named/temp_dir（mkstemp/mkdtemp，$TMPDIR 或 /tmp）、TempFile 的 ok/path/fd/write_all/close_file/remove，0600 + FD_CLOEXEC，不自动删除
  std/os/lock.c6            std.os.lock            文件建议锁（fcntl）：lock_open、FileLock 的 acquire(exclusive, wait_ms)/try_acquire/release/is_held/close_file；wait_ms<0 阻塞、0 只试一次、>0 重试到超时，整文件锁按进程合并
  std/os/mmap.c6            std.os.mmap            文件/匿名映射：map_file(path, writable)（可写 MAP_SHARED、只读 MAP_PRIVATE，空文件是坏映射）、map_anon（清零）、Map 的 data/size/sync/read_at/write_at/unmap；read_at/write_at 带边界检查

std/encoding/               编解码与压缩：Base32/Base64/Base85/十六进制/百分号、MD5/SHA-1/SHA-256/SHA-512、HKDF/PBKDF2、AES/ChaCha20/Poly1305、安全随机、DEFLATE/zlib/gzip/tar、位流、UUID、Punycode/IDNA、分帧、LZ4/RLE/Snappy、hexdump、DER/ASN.1、HOTP/TOTP、GIF 的 LZW
  std/encoding/base32.c6    std.encoding.base32    Base32（RFC 4648）：b32_encode/b32_decode/b32hex_encode/b32hex_decode、显式表与填充策略、b32_valid（跳过空白、接受省略填充与小写）
  std/encoding/base85.c6    std.encoding.base85    Ascii85（Adobe）与 Z85（ZeroMQ）：a85_encode/decode、a85_encoded_len/decoded_len/valid（`z` 简写、部分组、跳过空白、`~>` 终止），z85_encode/decode/valid（长度必须 4/5 的倍数、严格字母表）
  std/encoding/base58.c6    std.encoding.base58    Bitcoin Base58：b58_encode/b58_decode、b58_encoded_len（上界）/b58_valid（前导零写 1）
  std/encoding/varint.c6    std.encoding.varint    LEB128：uvarint/svarint 编解码、zigzag、*_encoded_len（解码返回 1/0/-1）
  std/encoding/base64.c6    std.encoding.base64    Base64（RFC 4648）标准表与 URL 安全表：b64_encode/b64_decode、b64url_encode/b64url_decode、显式表与填充的 b64_encode_ex/b64_decode_ex/b64_decoded_len/b64_encoded_len、b64_valid、就地 b64_decode_inplace/b64url_decode_inplace（跳过空白、严格填充、放不下不写半截）
  std/encoding/bitbuf.c6    std.encoding.bitbuf    位级读写器（零分配、MSB 先）：bw_open/put/put_u64/put_bit/align/put_bytes/patch（回填不改游标）、br_open/get/get_u64/get_bit/peek/skip/align/get_bytes、bit_count/byte_count/ok/remaining/eof
  std/encoding/codec.c6     std.encoding.codec     Base64、十六进制、百分号编解码（写调用方缓冲，放不下一个字节都不写）
  std/encoding/hexdump.c6   std.encoding.hexdump   hexdump 文本：hex_opts、hexdump/hexdump_len（width 1..64、大小写、ASCII 列、偏移基数）、hexundump 反向解析（认行首 8 位偏移、跳过 `|...|`）
  std/encoding/frame.c6     std.encoding.frame     串行分帧（零分配）：COBS 的 cobs_max_len/cobs_encoded_len/cobs_encode/cobs_decode/cobs_encode_frame/cobs_decode_frame 与 SLIP（RFC 1055）的 slip_encoded_len/slip_encode/slip_decode/slip_encode_frame/slip_decode_frame；坏格式/放不下都不写半截
  std/encoding/lz4.c6       std.encoding.lz4       LZ4 块压缩/解压（零分配）：lz4_compress（调用方给 int[lz4_table_size()] 哈希表）/lz4_decompress、lz4_compress_bound；兼容标准 LZ4 块格式，坏偏移/截断/放不下都拒绝
  std/encoding/rle.c6       std.encoding.rle       PackBits 游程（rle_encode/decode、rle_encoded_len/decoded_len、rle_bound）与一阶差分（delta_encode/decode，模 256）
  std/encoding/sha1.c6      std.encoding.sha1      SHA-1 与 HMAC-SHA1：流式 init/update/final、一次性 sha1/sha1_hex、hmac_sha1/hmac_sha1_hex、常数时间 sha1_eq（协议互操作：WebSocket 握手、git 对象名、HTTP Digest；抗碰撞请用 sha256）
  std/encoding/sha256.c6    std.encoding.sha256    SHA-256 与 HMAC-SHA256：流式 init/update/final、一次性 sha256_hex、常数时间比较
  std/encoding/md5.c6       std.encoding.md5       MD5（RFC 1321）：Md5 的 init/update/final、一次性 md5/md5_hex、常数时间 md5_eq；只用于校验和/兼容格式（碰撞是分钟级的）
  std/encoding/sha512.c6    std.encoding.sha512    SHA-512/SHA-384 与 HMAC-SHA-512：init/init384/update/final（128 位长度）、sha512/sha384/*_hex、hmac_sha384/hmac_sha512(_hex)、常数时间 sha512_eq
  std/encoding/sha3.c6       std.encoding.sha3      SHA3-224/256/384/512 与 SHAKE128/256（Keccak 海绵，可连续挤压）
  std/encoding/blake2s.c6    std.encoding.blake2s   BLAKE2s：输出 1~32 字节，init/update/final 与一次性 blake2s_hash
  std/encoding/crc32c.c6     std.encoding.crc32c    CRC-32C（Castagnoli）：crc32c/crc32c_str 与裸寄存器分段 crc32c_update
  std/encoding/xxhash.c6     std.encoding.xxhash    xxHash：xxh32/xxh64（带种子）与流式 Xxh32/Xxh64（digest 可重复）
  std/encoding/hkdf.c6      std.encoding.hkdf      HKDF（RFC 5869）：hkdf_extract/hkdf_expand/hkdf 一步版；空 salt 按 32 个零字节、输出上限 8160 字节
  std/encoding/pbkdf2.c6    std.encoding.pbkdf2    PBKDF2-HMAC-SHA-1/SHA-256（RFC 6070/8018）：pbkdf2_sha1/pbkdf2_sha256，iters<=0 与空输出直接拒绝，输出长度不限
  std/encoding/aes.c6       std.encoding.aes       AES（FIPS 197）：aes_key(16/24/32)、Aes 的 encrypt_block/decrypt_block/ctr_xor（10/12/14 轮，CTR 按 16 字节大端计数器递增）
  std/encoding/chacha20.c6  std.encoding.chacha20  ChaCha20（RFC 8439）：chacha20_block(key32, nonce12, counter, out64)、chacha20_xor（允许 in==out、跨块自动递增计数器）
  std/encoding/poly1305.c6  std.encoding.poly1305  Poly1305 一次性 MAC（RFC 8439）：poly1305_init 的 update/final、一次性 poly1305_mac；5×26 位肢体做 130 位模运算，同一把 key 只能用一次
  std/encoding/random.c6    std.encoding.random    安全随机（/dev/urandom）：rand_bytes/rand_u64/rand_below（掩码+拒绝采样）/rand_hex；每次 open/close，不缓存 fd
  std/encoding/deflate.c6   std.encoding.deflate   DEFLATE（RFC 1951）：inflate 支持 stored/固定/动态 Huffman、deflate_stored、deflate_fixed（LZ77 + 固定 Huffman，栈上哈希表）、deflate_bound
  std/encoding/zlib.c6      std.encoding.zlib      zlib 容器：adler32/adler32_update、zlib_compress（78 01 头 + DEFLATE + Adler-32）、zlib_decompress（校验头与尾）、zlib_bound
  std/encoding/gzip.c6      std.encoding.gzip      gzip 容器：gzip_crc32、gzip_compress(...,mtime)、gzip_decompress（跳过 FEXTRA/FNAME/FCOMMENT/FHCRC 并校验 CRC-32/ISIZE）、gzip_bound
  std/encoding/tar.c6       std.encoding.tar       tar（ustar）：tar_reader + next（checksum/prefix 拼接/数据视图）、tar_writer + add_file/add_dir/finish（八进制字段、512 对齐、两个零块）
  std/encoding/asn1.c6      std.encoding.asn1      DER/ASN.1 读取器：asn1/asn1_sub + next/peek 切 Asn1Tag（多字节 tag、0x81..0x84 长度、拒绝不定长/越界）、asn1_int/asn1_oid/asn1_bits/asn1_time/asn1_string
  std/encoding/otp.c6       std.encoding.otp       HOTP（RFC 4226）/TOTP（RFC 6238）：hotp/totp/otp_format、otp_verify_totp（前后各 window 步）；HMAC-SHA-1 动态截断，位数 4..9
  std/encoding/lzw.c6       std.encoding.lzw       GIF 变体 LZW：lzw_gif_decode（LSB、clear/eoi、码宽 2..12）/lzw_gif_encode（贪心最长匹配），坏流/越界/放不下返回 -1
  std/encoding/snappy.c6    std.encoding.snappy    Snappy 块格式：snappy_decode 严格校验、snappy_encode 用调用方 16 KB 哈希表贪心匹配（传 0 退化为字面量）
  std/encoding/punycode.c6  std.encoding.punycode  Punycode（RFC 3492）+ IDNA：punycode_encode/decode、idna_encode/decode（xn--、Unicode 句点、63/255 字节上限）
  std/encoding/uuid.c6      std.encoding.uuid      UUID（RFC 9562/4122）：uuid_v4/Rand.v4_seeded、uuid_v7/uuid_v7_at/time_v7、v5 与四个预定义命名空间 uuid_ns_dns/uuid_ns_url/uuid_ns_oid/uuid_ns_x500、uuid_parse/format/format_simple/format_upper、version/variant/eq/uuid_nil/is_nil

std/data/                   数据格式：JSON、CSV、INI、.properties/.env、TOML、XML、查询串/表单、Bencode、MessagePack、CBOR、PEM、X.509 证书、JWT
  std/data/csv.c6           std.data.csv           RFC 4180 CSV：零分配 CsvReader（引号/跨行/CRLF）+ csv_write_row
  std/data/ini.c6           std.data.ini           INI 配置的零分配读取：ini_open/next（字段都是输入上的视图）、ini_lookup/ini_has_section/ini_count、IniEntry.section/key/value/line；语法错误给行号
  std/data/json.c6          std.data.json          JSON 解析与写出：Arena 上的 DOM（a.json_parse）、Json 的 get/at、write/write_pretty/escape（数字保留原文）
  std/data/query.c6         std.data.query         URL 查询串 / form-urlencoded（零分配）：query_open + QueryReader.next、QueryPair 的 name/value/has_eq、query_decode（`+` 与 %XX）、query_encode/query_build/query_build_len、query_get/query_get_nth/query_has
  std/data/toml.c6          std.data.toml          TOML 子集零分配读取：toml_open + TomlReader.next、TomlEntry 的 kind/section/key/raw 与 decode、toml_int/toml_float/toml_bool、toml_array_len/toml_array_at；表/表数组、注释、基本与字面量字符串、数组、语法错误给行号
  std/data/xml.c6           std.data.xml           XML 零分配扫描：xml_open + XmlReader.next（START/END/SELF/TEXT）、XmlEvent 的 name/text/attrs 与 attr_count/attr_at/attr、xml_unescape（5 个预定义实体 + 数字引用）；跳过注释/PI/DOCTYPE，CDATA 当文本，32 层标签配对校验
  std/data/props.c6         std.data.props         .properties/.env 零分配读取：props_open/next、e.decode(buf)、props_lookup/has/count（\n/\uXXXX/代理对/续行）
  std/data/bencode.c6       std.data.bencode      Bencode（BitTorrent）：benc_writer 的 write_int/write_str/write_list/write_dict/write_end、benc_reader + next 的标记流；拒绝前导零与溢出
  std/data/msgpack.c6       std.data.msgpack      MessagePack：mp_writer 的 nil/bool/uint/int/f64/str/bin/array/map、mp_reader + next 的 MpTag；不支持扩展类型
  std/data/cbor.c6          std.data.cbor         CBOR（RFC 8949）：cbor_writer 的 uint/int/bytes/text/array/map/bool/null/f64、cbor_reader + next 的 CborTag；半/单/双精度都转 f64，不支持不定长与标签
  std/data/pem.c6           std.data.pem          PEM（RFC 7468）：pem_encode（64 列 Base64）、pem_reader + next 的多块迭代、PemBlock.decode_der；标签 1..48 个可打印 ASCII
  std/data/wav.c6           std.data.wav           RIFF/WAVE 头解析：wav_parse 给格式/声道/采样率/位宽/对齐/data 偏移，WavInfo.frames 算帧数，跳过 LIST/JUNK 等块
  std/data/png.c6           std.data.png           PNG 块遍历：png_reader + next 切块（类型/长度/偏移/CRC）、data 视图、png_parse 解析 IHDR；CRC 按 IEEE 校验
  std/data/elf.c6           std.data.elf           ELF 头与节表：elf_parse 支持 ELF32/64 大小端，elf_reader + next 给出节名/类型/flags/addr/offset/size（名字取自 shstrtab）
  std/data/zip.c6           std.data.zip           ZIP 解析：EOCD → 中央目录 → 本地头，stored/deflate 两种条目解压并校验 CRC；ZIP64/加密/未知方法 -1
  std/data/bmp.c6           std.data.bmp           Windows BMP 解码：bmp_parse（24/32 位真彩 + 8 位调色板、上下行序、4 字节行对齐）、bmp_decode 输出 RGBA8888
  std/data/tga.c6           std.data.tga           TGA 解码：类型 2/10（未压缩/RLE）真彩、24/32 位、四种原点；BGR(A)→RGBA8888
  std/data/qoi.c6           std.data.qoi           QOI 解码与编码：qoi_decode（INDEX/DIFF/LUMA/RUN/RGB/RGBA）、qoi_encode（RUN/INDEX/RGB/RGBA），RGBA8888
  std/data/pcx.c6           std.data.pcx           PCX：128 字节头 + RLE，8 位调色板（末尾 VGA 调色板优先）/24 位三平面/1 位单色，输出 RGBA
  std/data/hdr.c6           std.data.hdr           Radiance RGBE：头解析、新式扫描线 RLE 与老式平铺（含 1 1 1 count）、hdr_to_rgb 色调映射
  std/data/xbm.c6           std.data.xbm           X BitMap：从 #define *_width/*_height 取尺寸，解花括号里的字节，xbm_pixel 按 X11 位序
  std/data/xpm.c6           std.data.xpm           X PixMap：w h ncolors cpp + 颜色表 + 像素行，支持 None/#RGB/#RRGGBB/#RRRRGGGGBBBB
  std/data/bdf.c6           std.data.bdf           BDF 位图字体：FONT/SIZE/FONTBOUNDINGBOX/CHARS 头，DWIDTH/BBX 度量，十六进制位图（高位在左）
  std/data/woff.c6          std.data.woff          WOFF 1.0 容器：44 字节头 + 每表 20 字节目录，给出 offset/compLength/origLength/checksum
  std/data/svg.c6           std.data.svg           SVG 路径：d 规范化为 M/L/C/Q/A/Z（H/V 并入、S/T 反射），包围盒用控制点凸包
  std/data/ttf.c6           std.data.ttf           TrueType/OTF 度量：表目录 + head/hhea/maxp/cmap(format 4/12)/hmtx，ttf_glyph/ttf_advance
  std/data/mbr.c6           std.data.mbr           MBR/GPT 分区表：4 个 16 字节目录 + EFI PART 头 + UTF-16LE 表项名字
  std/data/fat.c6           std.data.fat           FAT12/16/32：BPB 字段（按簇数判类型）、根目录区、32 字节短目录项与按名查找
  std/data/iso.c6           std.data.iso           ISO 9660：主卷描述符（卷 ID/块大小/根目录记录）+ 目录记录 + 跨块扫描
  std/data/ext2.c6          std.data.ext2          ext2/3/4：偏移 1024 的超级块、块组描述符、按「1 起」inode 号取根目录
  std/encoding/base91.c6    std.encoding.base91    basE91 编解码：每 13 位两字符，>88 时退回 13 位，解码按值判位宽
  std/encoding/uu.c6        std.encoding.uu        uuencode/uudecode：45 字节/行、行首长度字符、` 代零，完整 begin/end 块
  std/encoding/yenc.c6      std.encoding.yenc      yEnc：+42 与 = 转义（结果落在 0x00/0x0A/0x0D/0x3D），=ybegin 头与 128 折行
  std/encoding/ebcdic.c6    std.encoding.ebcdic    EBCDIC CP037 ↔ ASCII：95 个可打印字符的双向表，表外写 ?/报错
  std/data/mp3.c6           std.data.mp3           MP3：ID3v2（synchsafe）+ 末尾 ID3v1 + MPEG 帧头（码率/采样率/帧长）
  std/data/ogg.c6           std.data.ogg           Ogg：27 字节页头 + 段表，BOS 首包识别 Vorbis/Opus/Speex/FLAC/Theora
  std/data/flac.c6          std.data.flac          FLAC：fLaC + 元数据块链，STREAMINFO 按位读采样率/声道/位深，VORBIS_COMMENT 取注释
  std/data/midi.c6          std.data.midi          MIDI：MThd/MTrk、变长量 delta、通道消息/meta/sysex 事件
  std/net/smtp.c6           std.net.smtp           SMTP：三位状态码 + 多行标志、命令拼装（拒 CRLF 注入）、DATA 点填充与 CRLF 归一
  std/net/ftp.c6            std.net.ftp            FTP：应答行、PASV 地址与端口、LIST 长格式与 MLSD 事实行
  std/net/redis.c6          std.net.redis          RESP：+/−/:/$/* 五种类型解析与 bulk/int/array/cmd 编码
  std/net/telnet.c6         std.net.telnet         Telnet：IAC 协商（WILL/WONT/DO/DONT/SB…SE）、IAC IAC 转义与去转义
  std/container/fenwick.c6 std.container.fenwick 树状数组：单点加、前缀和/区间和、按前缀和定位首个 >= target 的下标
  std/container/segtree.c6 std.container.segtree 迭代线段树（2n 数组）：求和/最小/最大，点赋值与点加，闭区间聚合
  std/container/dsu.c6      std.container.dsu      并查集：迭代路径减半、按大小合并、集合数与集合大小
  std/container/bloom.c6    std.container.bloom    布隆过滤器：FNV-1a 双哈希、置位统计与填充比例
  std/text/slug.c6          std.text.slug          URL slug：UTF-8 解码 + Latin-1 去音标，CJK 当分隔、连续分隔合一
  std/text/casing.c6        std.text.casing        命名风格互转：切词（缩略词边界）后输出 snake/kebab/camel/Pascal/title
  std/text/roman.c6         std.text.roman         罗马数字：1..3999 双向转换，回写比对保证只认标准写法
  std/text/phonetic.c6      std.text.phonetic      发音编码：Soundex 4 字符码（h/w 不打断）+ 简化 Metaphone + phone_same
  std/core/vector.c6        std.core.vector        矢量：Vec2/Vec3 加减缩放、点积/叉积、长度/归一化/插值/投影/夹角
  std/core/polynomial.c6    std.core.polynomial    多项式（系数升幂）：Horner 求值、导数、加减乘、二次求根
  std/core/fft.c6           std.core.fft           基 2 迭代 FFT：原地位反转 + 蝶形，逆变换带 1/n，幅度/功率谱
  std/core/spline.c6        std.core.spline        曲线插值：贝塞尔（含导数）、Hermite、Catmull-Rom、采样段数
  std/os/calendar.c6        std.os.calendar        公历换算：日期↔天数、闰年、星期、ISO 周（含跨年）
  std/os/iso8601.c6         std.os.iso8601         ISO 8601：解析（Z/±HH:MM/小数秒）与格式化、epoch 互转
  std/os/duration.c6        std.os.duration        时长：1h30m / 500ms / 1.5h / H:MM:SS 解析与格式化（毫秒）
  std/os/cron.c6            std.os.cron            cron 五字段：*、*/n、a-b/n、列表、星期名，匹配与下次运行时间
  std/net/ntp.c6            std.net.ntp            NTPv4：48 字节头、1900 纪元时间戳换算、RFC 5905 偏移/时延估计
  std/net/syslog.c6         std.net.syslog         syslog：RFC 5424 与 RFC 3164 解析（PRI/设施/级别/结构化数据）、5424 构造
  std/net/stun.c6           std.net.stun           STUN：位交错类型字段、TLV 属性、XOR-MAPPED-ADDRESS 解码与构造
  std/net/tftp.c6           std.net.tftp           TFTP：RRQ/WRQ/DATA/ACK/ERROR/OACK 解析与构造
  std/os/log.c6             std.os.log            定长环形日志：TRACE..FATAL 级别过滤、覆盖/过滤计数、dump 格式化
  std/data/sqlite.c6        std.data.sqlite       SQLite：100 字节文件头、B-tree 页头与单元（payload/rowid）、1..9 字节变长整数
  std/net/postgres.c6       std.net.postgres      PostgreSQL v3：报文拆帧、启动包键值对、ErrorResponse 字段、构造查询/口令
  std/net/mysql.c6          std.net.mysql         MySQL：3 字节小端长度报文、HandshakeV10、长度编码整数、COM_QUERY
  std/encoding/fnv.c6        std.encoding.fnv       FNV-1/FNV-1a 32/64 位：一次性 + 流式状态，空输入为偏移基数
  std/encoding/adler32.c6    std.encoding.adler32   Adler-32：初值 1、5552 字节分块取模、流式更新与两段合并
  std/encoding/murmur3.c6    std.encoding.murmur3   MurmurHash3：x86 32 位与 x64 128 位（两个 64 位半），支持 seed
  std/encoding/siphash.c6    std.encoding.siphash   SipHash-2-4/1-3：16 字节密钥、流式状态、定时安全比较
  std/container/treap.c6    std.container.treap    Treap：split/merge 实现插入删除，带 rank（严格小于）与 kth（第 k 小）
  std/container/heap.c6     std.container.heap     二叉堆：最小/最大、O(n) 堆化、pushpop（top-k）、原地堆排序
  std/container/skiplist.c6 std.container.skiplist 8 层跳表：有序 map，首元素/后继遍历与按序号取
  std/container/counter.c6  std.container.counter  频次计数器：开放寻址，键定长 23 字节，取出现最多的键
  std/encoding/bwt.c6        std.encoding.bwt       Burrows-Wheeler：倍增+计数排序排循环移位，LF 映射逆变换
  std/encoding/mtf.c6        std.encoding.mtf       Move-To-Front：输出符号表位置并前移，BWT 之后把重复变成 0
  std/encoding/delta.c6      std.encoding.delta     差分编码：字节（模 256）与 i64 数组两种，适合单调序列
  std/encoding/huffman.c6    std.encoding.huffman   规范 Huffman：频率建树→码长→规范码，Kraft 校验，位打包编解码
  std/data/srt.c6           std.data.srt           SubRip/WebVTT：游标方法 open/next/count/seek，时间解析与单条生成
  std/data/ass.c6           std.data.ass           ASS/SSA：Script Info 与 Events，Dialogue/Comment 事件（Text 取到行尾）
  std/data/lrc.c6           std.data.lrc           LRC 歌词：ti/ar/al/offset 元信息 + [mm:ss.xx] 行，offset 自动加到时间上
  std/data/m3u.c6           std.data.m3u           M3U/M3U8：#EXTM3U/#EXTINF（秒→毫秒、标题）与 #EXT-X- 的 HLS 判断
  std/data/netrc.c6          std.data.netrc        .netrc 凭据：machine/default 条目 + login/password，find 先精确再兜底
  std/data/sshconfig.c6      std.data.sshconfig    OpenSSH 配置：Host 分块、多模式/!/通配匹配、块内按键取值
  std/data/keys.c6           std.data.keys         authorized_keys 行：引号选项、类型、base64 blob、注释与位数提示
  std/data/crontab.c6        std.data.crontab      crontab：五段定时行、@reboot 别名、NAME=value 环境变量（双游标）
  std/core/geo.c6           std.core.geo           球面几何：haversine 距离、初始方位、目的地、半径包围盒（box.contains/expand）
  std/data/geohash.c6       std.data.geohash       Geohash：base32 编码/解码、格子边界、邻居（跨日界线折回）
  std/data/maidenhead.c6    std.data.maidenhead    Maidenhead 网格：2/4/6/8 位（20°×10° 到 0.5′×0.25′）编解码
  std/data/dms.c6           std.data.dms           度分秒：灵活解析 + to_deg/from_deg/format 方法与 N/S/E/W 方向
  std/data/ar.c6            std.data.ar            Unix ar 归档：60 字节成员头、GNU 长名（// + /123）与 BSD #1/N，find 按名查
  std/data/coff.c6          std.data.coff          MS COFF：20 字节头 + 40 字节节头 + 18 字节符号表（含字符串表长名）
  std/data/class.c6         std.data.class         Java .class：CAFEBABE、常量池 14 种 tag（Long/Double 占两槽）、方法与描述符
  std/data/rpm.c6           std.data.rpm           RPM 包：96 字节 lead + 主头索引表（tag/type/offset/count），tag_name 命名
  std/encoding/luhn.c6      std.encoding.luhn     校验位：Luhn、ISBN-10（X 校验位）、ISBN-13、Verhoeff 的 check/digit
  std/encoding/crc16.c6     std.encoding.crc16    CRC-8 与 CRC-16（CCITT/XMODEM/MODBUS/ARC），流式方法 init/update/final
  std/encoding/hamming.c6   std.encoding.hamming  汉明码 (7,4)：block 方法 encode/syndrome/fix/decode，可纠 1 位错
  std/encoding/classic.c6   std.encoding.classic  经典密码：凯撒/ROT13/Atbash/维吉尼亚/异或/栅栏（只看 ASCII 字母）
  std/os/mounts.c6          std.os.mounts          /proc/mounts：六字段挂载表 + \040 八进制反转义（unescape）
  std/os/fstab.c6           std.os.fstab           /etc/fstab：4~6 字段、# 注释、dump/passno 默认 0、find 按挂载点查
  std/os/osrelease.c6       std.os.osrelease       /etc/os-release：KEY=VALUE、引号剥离、键值裁剪，find 按名取
  std/os/systemd.c6         std.os.systemd         systemd unit：[Section] + Key=Value，注释 #/;，get 按段+键取值
  std/core/binio.c6         std.core.binio         整数读写游标：Reader/Writer 方法 u16le/u24le/u32be/u64…，越界只置 ok=0
  std/core/bitio.c6         std.core.bitio         按位读写：BitReader/BitWriter 方法 read/write/align，高位在前
  std/core/half.c6          std.core.half          fp16 与 bfloat16：Half 方法 set/get/is_nan，次正规数与 ±0/inf/NaN
  std/core/align.c6         std.core.align         对齐工具：align_up/down、padding_for、is_pow2、round_up、next_pow2、log2_floor
  std/data/pnm.c6           std.data.pnm           Netpbm P5/P6 解码：注释、maxval 1..65535、8/16 位大端样本按 v*255/maxval 缩放到 RGBA
  std/data/x509.c6          std.data.x509          X.509 v3 证书解析：x509_parse 给出版本/序列号/签名 OID/issuer·subject 的 CN·O·C/有效期/SPKI/RSA 公钥位数/签名视图；不验签、不验链、不处理 PEM
  std/data/jwt.c6           std.data.jwt           JWS 紧凑格式（JWT）验签：jwt_verify 解头/载荷，HS256/HS384/HS512 常量时间比较签名；alg=none 与非 HMAC 拒绝，不解析 JSON
  std/data/gif.c6           std.data.gif           GIF87a/89a 解码：gif_info/gif_decode 按索引取帧解成 RGBA，支持全局/局部调色板、透明色、隔行扫描、多帧
  std/data/tiff.c6          std.data.tiff          基线 TIFF 解码：II/MM 字节序、STRIP、8 位、none/Deflate(zlib)/PackBits、RGB/灰度 → RGBA；LZW/16 位/分块不支持
  std/data/ico.c6           std.data.ico           Windows 图标：ico_info/ico_entry/ico_decode，DIB 1/4/8/24/32 位（AND 掩码透明）或内嵌 PNG
  std/data/html.c6          std.data.html          HTML 分词器：html_open/next 出文本/开始/结束/注释/DOCTYPE/CDATA，html_attr 按需扫属性，html_unescape 解实体；不建 DOM
  std/data/yaml.c6          std.data.yaml          YAML 常用子集：yaml_open/next 出 `键: 值`、`- 值`、`- 键: 值` + depth，yaml_unquote 解引号；不支持流式/锚点/块标量
  std/data/vcf.c6           std.data.vcf           vCard：vcard_unfold 摊平折行 + vcard_parse 取 FN/N/ORG/TITLE/TEL/EMAIL/UID/URL
  std/data/ics.c6           std.data.ics           iCalendar：ics_unfold 摊平折行 + ics_open/next 逐 VEVENT 读 UID/SUMMARY/DTSTART/DTEND/LOCATION
  std/data/cpio.c6          std.data.cpio          cpio 归档（newc 070701 / crc 070702）：cpio_open/next 逐项给 mode/size/name/data_off，cpio_find 按名字找
  std/data/pe.c6            std.data.pe            PE/COFF 头：pe_info（DOS→e_lfanew→PE→COFF→可选头，PE32/PE32+）、pe_section、pe_rva_to_off、machine/subsystem 名字
  std/data/macho.c6         std.data.macho         Mach-O 头：thin 32/64 位与 fat 外壳，macho_info 抓 LC_UUID/LC_MAIN，macho_section 摊平各段节表，macho_arch 列架构
  std/data/wasm.c6          std.data.wasm          WebAssembly 结构：wasm_open/wasm_reader/next 逐节、wasm_leb_u32、wasm_count、wasm_export_at
  std/data/bson.c6          std.data.bson          BSON 文档：bson_open/next 逐元素，bson_string/int64/double/bool/binary/objectid，bson_subdoc 进子文档与数组，bson_find 按名字找
  std/data/ubjson.c6        std.data.ubjson        UBJSON：ubj_open/next 事件流（标量 + ARRAY/OBJECT/KEY/END），支持 $ 统一类型与 # 计数优化容器
  std/data/plist.c6         std.data.plist         Apple 二进制 plist（bplist00）：plist_open/root/count/at/key/get + 各标量访问器，走偏移表与引用号
  std/text/markdown.c6      std.text.markdown      Markdown 常用子集 → HTML：markdown_render（标题/段落/围栏代码/引用/列表/分隔线）与 markdown_inline（代码/粗斜体/链接/转义）

std/net/                    网络与 Web：TCP/UDP 套接字、本地套接字、DNS 报文与查询、HTTP/1.x 解析、客户端与服务端骨架、路由、静态资源、会话、WebSocket 帧、IP/CIDR、URL、MIME、multipart
  std/net/cookie.c6         std.net.cookie         Cookie/Set-Cookie 零分配解析：cookie_parse_set/pair、Cookie 的 attribute/path/domain/max_age/is_secure、cookie_write_pair
  std/net/http.c6           std.net.http           HTTP/1.x 零分配解析：http_parse_request/response、HttpMessage 的 header/header_at/body/content_length、http_status_text
  std/net/inet.c6           std.net.inet           IP 地址与 CIDR（零分配）：inet_parse/inet_parse_v4/inet_parse_v6，Ip/Ipv4/Ipv6 的 format（IPv6 按 RFC 5952 压缩）、inet_parse_cidr/cidr_contains/cidr_mask/inet_v4_mask、is_private/is_loopback/is_link_local/is_multicast/is_unspecified、inet_parse_port
  std/net/mime.c6           std.net.mime           扩展名 ↔ MIME 类型、分类与魔数嗅探（零分配）：mime_of_ext/mime_of_path/mime_ext_of/mime_ext_at/mime_type_at/mime_count、mime_is_text/is_image/is_audio/is_video/is_font、mime_charset、mime_sniff、mime_default
  std/net/multipart.c6      std.net.multipart      multipart/form-data 零分配解析：mp_open/mp_open_boundary、Multipart 的 next/count、Part 的 name/filename/content_type/body、mp_header/mp_param
  std/net/dns.c6            std.net.dns            DNS 报文（RFC 1035）：dns_header、dns_question/dns_rr（压缩指针、*next 遍历）、dns_a/dns_aaaa/dns_cname、dns_build_query、dns_type_name/dns_rcode_name
  std/net/tcp.c6            std.net.tcp            TCP 套接字（POSIX）：tcp_listen/tcp_connect（超时）/accept_client、Tcp 的 write_bytes/write_all/read_bytes/read_all/set_timeout/set_nodelay/shutdown_write/close_socket、port/ok
  std/net/ws.c6             std.net.ws             WebSocket（RFC 6455）：ws_accept 握手、ws_encode/ws_parse/ws_decode 帧（FIN/opcode/7-16-64 位长度/掩码）、WsFrame
  std/net/url.c6            std.net.url            URL（RFC 3986）解析与相对解析：Url 的 url_parse/effective_port/is_absolute/has_host、url_resolve（§5.2 全算法）、查询参数 next_pair/Pair、百分号 url_encode/url_encode_component/url_decode/url_decode_at/url_encoded_len/url_decoded_len
  std/net/udp.c6            std.net.udp            UDP 套接字（POSIX）：udp_bind/udp_connect/udp_bad、Udp 的 send_to/send_bytes/recv_bytes/recv_from/set_timeout/set_broadcast/port/ok/close_socket、UdpAddr（超时 -1、空报文 0）
  std/net/uds.c6            std.net.uds            AF_UNIX 本地套接字：uds_pair/uds_listen/uds_connect/accept_client、Uds 的 write_bytes/write_all/read_bytes/read_all/set_timeout/shutdown_write/close_socket、UdsPair 的 ok_pair/close_pair（EOF 返回 0）
  std/net/resolver.c6       std.net.resolver       DNS 查询客户端：resolver_open/resolver_from_server、Resolver 的 resolve_a/resolve_aaaa/resolve_ip/set_timeout/ok、parse_reply（校验 id/TC/rcode）、resolver_conf_parse/resolver_conf_file、ipv4_format/ipv6_format
  std/net/httpc.c6          std.net.httpc          HTTP/1.1 客户端：build_request（先量再写）/fetch/parse_response（1/0/-1）、Response 的 is_ok/header/header_int 与 status/reason/body/chunked/content_length/needs_close，支持 chunked 解码（只做 http://）
  std/net/httpd.c6          std.net.httpd          HTTP/1.1 服务端骨架：httpd_listen/accept_conn、Conn 的 read_request（1/0/-1，body 只按 Content-Length）/begin/send_body/respond/respond_text、Request 的 method/path/query/body/header、serve_once(handler, ud)（一次连接一个请求）
  std/net/router.c6         std.net.router         方法与路径路由表（固定 32 条）：Router 的 add/match/count/clear、Match 的 param/has/rest/wildcard/id；:name 段参数、*/*name 尾段通配、方法大小写不敏感、先注册先赢、按 / 严格切段
  std/net/assets.c6         std.net.assets         静态资源：assets_root、Assets 的 resolve（1/0/-1）/serve（200/403/404/500，流式）/set_index/set_follow_links、content_type_of；解码后拒 .. 与 NUL、归一后必须在 root 下、默认拒符号链接
  std/net/session.c6        std.net.session        内存会话（16 槽）：session_store/session_store_ttl、Store 的 create/find/destroy/destroy_all/expire/cookie/from_cookie、Session 的 id/get/set/has/remove/clear/valid；cookie 名 sid，id 来自 std.core.rand（非密码学）
  std/net/sse.c6            std.net.sse            Server-Sent Events：sse_open/next 按空行派发事件，多行 data 拼进调用方缓冲；注释/BOM/三种行结束符
  std/net/socks5.c6         std.net.socks5         SOCKS5 报文：问候/用户名口令/请求构建 + 方法/认证/应答/地址解析，IPv4/IPv6/域名
  std/net/mqtt.c6           std.net.mqtt           MQTT 3.1.1 报文：固定头与变长剩余长度、CONNECT/PUBLISH/SUBSCRIBE 构建解析、+ / # 主题通配
```

```c
import std.container.vec;
import std.text.fmt;

@Vect@int v = new Vect@int;                /* 托管字段 -> new 出来 */
Buf@(32) b = { { 0 }, 0 };                 /* 纯值，栈上就够 */

v->push(42) catch e { return e.id; };      /* 失败必须被 try/catch 消费 */
b->write_int(v->count()) catch e { return 2; };
printf("%s\n", b->cstr());
```

`import` 只带来声明，所以模块自己的 `.c6` 文件要一起编进程序（`std/container/vec.c6`
的排序内核在 `std/container/sort.c6` 里，sort 又依赖 str/err/types，用 vec 就得把这几个
正文都放上命令行）：

```sh
./tcc/c6 -B tcc -I . -o build/app app.c6 \
    std/container/vec.c6 std/text/fmt.c6 std/core/types.c6 std/text/str.c6 std/container/sort.c6 std/core/err.c6
make stdtest     # 跑 std/ 下全部 test 块（模块数与块数以实际输出为准）
make std-demo    # 编译并运行 examples/wordcount.c6（用到 8 个模块）
make std-tour    # 编译并运行 examples/stdtour.c6（第二批模块的拼装示例）
make std-kit     # 编译并运行 examples/stdkit.c6（第三批模块的拼装示例）
make std-box     # 编译并运行 examples/stdbox.c6（第四批模块的拼装示例）
make std-embed   # 编译并运行 examples/stdembed.c6（第十一批嵌入式模块的拼装示例）
make std-text    # 编译并运行 examples/stdtext.c6（第十二批文本算法模块的拼装示例）
make std-num     # 编译并运行 examples/stdnum.c6（第十三批数值类型模块的拼装示例）
make std-pack    # 编译并运行 examples/stdpack.c6（第十四批压缩与文本编码模块的拼装示例）
make std-conf    # 编译并运行 examples/stdconf.c6（第十五批配置与文档格式模块的拼装示例）
make std-cli     # 编译并运行 examples/stdcli.c6（第十六批命令行工具基础模块的拼装示例）
make std-wire    # 编译并运行 examples/stdwire.c6（第十七批网络协议基础模块的拼装示例）
make std-algo    # 编译并运行 examples/stdalgo.c6（第十八批算法与容器模块的拼装示例）
make std-thread  # 编译并运行 examples/stdthread.c6（第十九批并发与同步模块的拼装示例）
make std-term    # 编译并运行 examples/stdterm.c6（第二十批终端与进程控制模块的拼装示例）
make std-client  # 编译并运行 examples/stdclient.c6（第二十一批网络客户端模块的拼装示例）
make std-serve   # 编译并运行 examples/stdserve.c6（第二十二批 Web 服务端模块的拼装示例）
make std-crypto  # 编译并运行 examples/stdcrypto.c6（第二十三批摘要与密钥派生模块的拼装示例）
make std-fsx     # 编译并运行 examples/stdfsx.c6（第二十四批文件系统进阶模块的拼装示例）
make std-cipher  # 编译并运行 examples/stdcipher.c6（第二十五批对称加密基础模块的拼装示例）
make std-data    # 编译并运行 examples/stddata.c6（第二十六批数据格式模块的拼装示例）
make std-calc    # 编译并运行 examples/stdcalc.c6（第二十七批数值工具模块的拼装示例）
make std-arch    # 编译并运行 examples/stdarch.c6（第二十八批压缩与归档模块的拼装示例）
make std-bin     # 编译并运行 examples/stdbin.c6（第二十九批二进制文件格式模块的拼装示例）
make std-img     # 编译并运行 examples/stdimg.c6（第三十批图像解码模块的拼装示例）
make std-enc     # 编译并运行 examples/stdenc.c6（第三十一批编码与凭据模块的拼装示例）
make std-gfx     # 编译并运行 examples/stdgfx.c6（第三十二批图像与 LZW 模块的拼装示例）
make std-doc     # 编译并运行 examples/stddoc.c6（第三十三批结构化文本格式模块的拼装示例）
make std-proto   # 编译并运行 examples/stdproto.c6（第三十四批网络协议编解码模块的拼装示例）
make std-exec    # 编译并运行 examples/stdexec.c6（第三十五批二进制容器格式模块的拼装示例）
make std-ser     # 编译并运行 examples/stdser.c6（第三十六批序列化与标记模块的拼装示例）
make std-sys     # 编译并运行 examples/stdsys.c6（第三十七批系统视图模块的拼装示例）
make std-hash    # 编译并运行 examples/stdhash.c6（第三十八批哈希与校验模块的拼装示例）
make std-legacy  # 编译并运行 examples/stdlegacy.c6（第三十九批老图形格式模块的拼装示例）
make std-font    # 编译并运行 examples/stdfont.c6（第四十批字体与矢量模块的拼装示例）
make std-disk    # 编译并运行 examples/stddisk.c6（第四十一批分区与文件系统模块的拼装示例）
make std-transcode # 编译并运行 examples/stdtranscode.c6（第四十二批老式文本编码模块的拼装示例）
make std-audio    # 编译并运行 examples/stdaudio.c6（第四十三批音频容器模块的拼装示例）
make std-proto2   # 编译并运行 examples/stdproto2.c6（第四十四批文本行协议模块的拼装示例）
make std-ds       # 编译并运行 examples/stdstruct.c6（第四十五批基础数据结构模块的拼装示例）
make std-words    # 编译并运行 examples/stdwords.c6（第四十六批文本变换模块的拼装示例）
make std-math2    # 编译并运行 examples/stdnumeric.c6（第四十七批数值工具模块的拼装示例，链接带 -lm）
make std-time     # 编译并运行 examples/stdtime.c6（第四十八批时间工具模块的拼装示例）
make std-packet   # 编译并运行 examples/stdpacket.c6（第四十九批网络报文模块的拼装示例）
make std-db       # 编译并运行 examples/stddb.c6（第五十批日志与数据库模块的拼装示例）
make std-hash2    # 编译并运行 examples/stdhash2.c6（第五十一批非加密散列模块的拼装示例）
make std-ordered  # 编译并运行 examples/stdordered.c6（第五十二批有序容器模块的拼装示例）
make std-bwt      # 编译并运行 examples/stdcmp.c6（第五十三批变换式压缩积木模块的拼装示例）
make std-subs     # 编译并运行 examples/stdsubs.c6（第五十四批字幕与播放列表模块的拼装示例）
make std-creds    # 编译并运行 examples/stdcreds.c6（第五十五批凭据与配置模块的拼装示例）
make std-geo      # 编译并运行 examples/stdgeo.c6（第五十六批地理编码模块的拼装示例，链接带 -lm）
make std-pkg      # 编译并运行 examples/stdpkg.c6（第五十七批目标文件与包模块的拼装示例）
make std-check    # 编译并运行 examples/stdcheck.c6（第五十八批校验与纠错模块的拼装示例）
make std-linux    # 编译并运行 examples/stdlinux.c6（第五十九批 Linux 配置文本模块的拼装示例）
make std-bin2     # 编译并运行 examples/stdbin2.c6（第六十批二进制与数值工具模块的拼装示例，链接带 -lm）
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
