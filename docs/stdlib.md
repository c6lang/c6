# c6 标准库 `std/`

标准库放在仓库根的 `std/` 下，**一个文件一个模块**，模块名就是它的路径：
`std/vec.c6` 声明 `module std.vec;`，消费侧写 `import std.vec;`（编译时把仓库
根放进 `-I`，或就在仓库根下编译）。

```sh
./tcc/c6 -B tcc -I . -I examples -o build/wordcount \
    examples/wordcount.c6 examples/wordcount/text.c6 \
    std/types.c6 std/err.c6 std/str.c6 std/sort.c6 \
    std/vec.c6 std/span.c6 std/fmt.c6 std/range.c6
```

> `import` 只带来**声明**：模块的函数体要把它自己的 `.c6` 文件一起编进程序。
> 上例里用到哪个 `std/*.c6` 就编哪个；`make std-demo` 是这条命令的缩写。
>
> 模块之间有依赖：`std/vec.c6` 的 `sort`/`sort_lex` 用 `std/sort.c6` 的内核，
> 而 `std/sort.c6` 又 `import std.str`/`std.err`/`std.types`——所以用
> `std.vec` 时这几份正文都得在命令行上，漏了会报
> `unresolved reference to 'std__sort__sort_bytes'`。

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `std/types.c6` | `std.types` | 定宽类型 `u8…u64`/`i8…i64`、`usize`/`isize`、`f32`/`f64`、`byte`、`bool`，以及 `Limits`（`U8_MAX`…`I32_MIN`）/`Bool` 两个枚举 |
| `std/err.c6` | `std.err` | 错误码 `enum Error`（`OK`…`BUSY`，16 个）与 `msg()`/`msg_of()`/`is_ok()`；号段固定在 `0x6000` |
| `std/vec.c6` | `std.vec` | 会长的数组 `Vect@T`（托管引用，`new` 出来）与 `Iter@T`：`push`/`pop`、`insert`/`remove_at`/`truncate`、`first`/`last`、`index_of`/`contains`、`reverse`、`slice`/`extend`、`sort`/`sort_lex`、`each`/`filter`/`fold` |
| `std/span.c6` | `std.span` | 别人内存上的窗口 `Span@T`（纯值，零拷贝） |
| `std/map.c6` | `std.map` | 开放寻址哈希表 `Map@(K, V)`、`Entry@(K, V)`、`Iter@(K, V)`：`put`/`get`/`get_or`/`set`/`put_new`/`remove`/`clear` |
| `std/str.c6` | `std.str` | 字符串视图 `Str`（`char[:]` 的包装，不分配）与字面量宏 `S()`：切分 `split`/`lines`/`join`、查找 `find`/`last_find`/`contains`/`count_char`/`count_sub`、裁剪 `trim`/`trim_left`/`trim_right`、转换 `to_int`/`to_base`/`copy_upper`/`copy_lower`、`cmp`/`cmp_str`/`cmp_cstr`、`replace`/`split_once`、`hash` |
| `std/fmt.c6` | `std.fmt` | 定长格式化缓冲 `Buf@(N)`：`write_int`/`write_uint`/`write_u64`、`write_hex`/`write_bin`、`write_bool`/`write_ptr`、`write_pad`、拼接 |
| `std/range.c6` | `std.range` | 能被 `foreach` 吃掉的区间 `Range`/`StepRange` |
| `std/sort.c6` | `std.sort` | 比较器驱动的排序与查找：`sort_bytes`/`sort_int`/`sort_u32`/`sort_f64`（稳定归并 + 小数组插入）、`find_bytes`/`find_int`/`find_u32`、`lower_bound_int`/`upper_bound_int`、`cmp_int`/`cmp_u32`/`cmp_f64`/`cmp_int_desc`、`is_sorted_int` |
| `std/mem.c6` | `std.mem` | 借用式内存池 `Arena`（`alloc`/`alloc_array`/`reset`/`room`/`used`）、托管堆薄包装 `alloc_new`/`alloc_new_array`/`dealloc`、按元素的 `copy`/`fill`/`zero`/`swap`/`rotate`/`reverse_bytes`、`align_up`/`align_down`/`is_aligned` |
| `std/io.c6` | `std.io` | 文件与标准流：`Reader`（`read_line`/`read_all`/`read_bytes`/`seek_to`/`tell`/`eof`）、`Writer`（`write_line`/`write_bytes`/`write_slice`/`flush`）、零分配的按行迭代 `CFile` |
| `std/math.c6` | `std.math` | 整数：`min`/`max`/`clamp`/`iabs`/`gcd`/`lcm`/`ipow`/`ipow_u64`/`imul_checked`/`sat_add`/`sat_mul`/`is_pow2`/`next_pow2`/`floor_div`/`mod`；浮点（不链 libm）：`sqrt`/`floor`/`ceil`/`trunc`/`round`/`fabs`/`nearly_eq`/`clamp_f64`/`fact` |
| `std/time.c6` | `std.time` | `now_ns`/`now_ms`/`now_unix`/`now_unix_ms`、`sleep_ms`/`sleep_ns`、`Duration` 拆分与 `format_duration`、`Civil` 日历换算 `civil_from_unix`/`unix_from_civil`/`format_civil`（不依赖 localtime） |
| `examples/wordcount.c6` | （无模块） | 把上面几块拼起来的示例程序：入口 `main` 只能写在**不属于任何模块**的文件里 |

每个 `std/*.c6` 都自带 `test` 块，`make stdtest` 逐个跑（140 个断言）：

```
std/err.c6       3 test(s), 0 failure(s)
std/fmt.c6       8 test(s), 0 failure(s)
std/io.c6        5 test(s), 0 failure(s)
std/map.c6       8 test(s), 0 failure(s)
std/math.c6      6 test(s), 0 failure(s)
std/mem.c6       7 test(s), 0 failure(s)
std/range.c6     6 test(s), 0 failure(s)
std/sort.c6     26 test(s), 0 failure(s)
std/span.c6      6 test(s), 0 failure(s)
std/str.c6      18 test(s), 0 failure(s)
std/time.c6      7 test(s), 0 failure(s)
std/types.c6     4 test(s), 0 failure(s)
std/vec.c6      36 test(s), 0 failure(s)
```

`-test` 的计数是命令行上**所有 TU** 的 `test` 块之和，所以带依赖跑的行会把自己
和依赖的块一起算进去：`std/sort.c6` 那行 = str 18 + sort 8，`std/vec.c6` 那行
= str 18 + sort 8 + vec 10。

## 设计原则

**1. 值语义优先，分配只发生在明确的地方。**
`Str`、`Span@T`、`Buf@(N)`、`Range` 都是纯值：没有指针、没有托管字段，
可以按值声明、按值传参、放进别的结构里，栈上就够。要「会自己长大」的
东西（`Vect@T`、`Map@(K, V)`）才用托管引用 `@T` + `new`——它们的生命周期
和释放由引用计数来管，调用方不用写 `free`。

**2. 不分配的东西不该失败，会失败的写 `T!error`。**
`span->at(i)` 越界返回 `0`/不写；`vec->push(v)` 可能没有内存，于是返回
`int!error`，调用方必须 `try` 或 `catch` 消费（编译器强制，见
`docs/grammar.md` C15）。这条分工让「几乎不会失败」的路径保持零开销，
同时不会悄悄丢掉失败。

**3. 容量由调用方给。**
`Buf@(N)` 的容量是类型的一部分，`Span@T` 不拥有内存，`Vect` 的扩容策略
（翻倍）写在库里。C 的调用方本来就清楚上限在哪，把上限写进类型比运行时
动态增长更容易推理，也更省。

**4. 名字跟语言一致。**
容器方法一律 `count()`（不叫 `len()`：字段已经叫 `len`/`n` 的不能同名）、
`is_empty()`、`at()`/`get()`/`set()`、`push()`/`pop()`。错误码用
`enum Error`，名字是 `INVAL`/`RANGE`/`NOMEM` 这种 C 风格的短名。

## 用法

### 定宽类型与错误码

```c
import std.types;
import std.err;

u32 bytes = 0;
i64 total = 0;

if (!is_ok(code))
    printf("错误：%s\n", msg_of(code));       /* 或 msg(code) */
```

`std.err.Error` 是普通枚举，模块外写 `std.err.NOMEM`，模块内写 `NOMEM`。
std 模块的号段内置在编译器里（`std.err` = `0x6000`），所以 `std.err.NOMEM`
的真实值是 `0x0E000003`：

```
id = (area << 27) | (slot << 12) | code     // area=1(std), slot=0x6000, code=3
```

这个号在**任何**程序里都一样，不需要 `errbase.def`，也永远不参与自动池的
哈希与撞号检查；std 模块的号段表见 [grammar.md](grammar.md) §8.1。

### `Vect@T`：托管容器

```c
import std.vec;

@Vect@int v = new Vect@int;              /* 托管字段 -> 必须 new */

v->push(10) catch e { return e.id; };    /* 普通函数里：取出错误码 */
if (v->count() != 1) ...
int first = v->at(0);                    /* 越界给 0；要判断用 get() */
foreach (int x = v->iter()) {
    printf("%d\n", x);
}
```

`Vect@T` 有托管字段 `@T data`，所以**不能**按值声明（`Vect@int v;` 编不过），
必须 `new` 出来、用 `@Vect@int` 持有；方法用 `v->m()`。
`at()` 不失败也不报错（越界给零值），要区分「0」和「越界」用
`get(i, &out)`，它的返回值告诉你有没有取到。

改内容用 `insert(i, v)`（`i` 可以等于 `count()`，等价于 `push`）、
`remove_at(i)`（删掉给 1，越界给 0）、`truncate(k)`（只留前 k 个，容量不动）、
`reverse()`、`extend(slice)`；看内容用 `first(&out)`/`last(&out)`（空容器给 0）、
`index_of(v)`（按字节比较，找不到给 -1）、`contains(v)`、`slice()`（当前内容的
`T[:]`，只读它，别拿它扩容后继续用）。

### `Span@T`：不拥有内存的窗口

```c
import std.span;

int a[5] = { 1, 2, 3, 4, 5 };
Span@int s = { a };                      /* 聚合初始化：视图就是数组本身 */
Span@int *p = &s;                        /* 值容器，方法要走指针 */

int n = p->count();                      /* 5 */
Span@int t = p->sub(1, 3);               /* { 2, 3, 4 }，不复制 */
printf("%d\n", (&t)->at(0));
```

`Span` 是纯值（一个切片），所以能按值声明；但方法接收者是指针，
值容器调用写 `(&s)->m()`。`sub()`/`drop()`/`take()` 只调整边界，零拷贝。

### `Map@(K, V)`：开放寻址哈希表

```c
import std.map;

@Map@(int, int) m = new Map@(int, int);

m->put(1, 100) catch e { return e.id; };
int out = 0;
if (m->get(1, &out) == 0) ...            /* 0 = 没有这个键 */
m->put(1, 200) catch e { return e.id; };  /* 覆盖，不增加条目 */
m->has(1);
m->remove(1);                            /* 留墓碑，之后还能插同一个键 */

Iter@(int, int) iv = { m, 0, { 0, 0 } }; /* 迭代器是纯值：聚合初始化 */
Iter@(int, int) *it = &iv;
while (it->has()) {
    int v = it->next();
    int k = it->cur.key;                 /* next() 填好的那一格 */
}
```

键按**字节**求哈希（FNV-1a），相等也按字节比较：键是 `int`、指针或者不含
填充的小结构体都对；含指针/填充的大结构体要自己保证字节可比。
`Map` 没有 `iter()` 方法——模板体内的方法还不能返回多参数泛型实例，
迭代器直接聚合初始化（原因见下面「已知限制」）。

### `Str`：字符串视图

```c
import std.str;

Str s = str(c_string);                   /* 要 strlen() */
Str t = { S("hello") };                  /* 字面量：长度不含结尾 NUL */
Str *p = &t;

p->count();  p->is_empty();
p->eq(S("hello"))  p->cmp(other.s)        /* 比 Slice，参数写 .s */
p->find(S("ll"))  p->find_char('l')       /* 找不到给 -1 */
p->starts_with(S("he"))  p->ends_with(S("lo"))
Str head = p->take(2);  Str tail = p->drop(2);  Str mid = p->sub(1, 3);
Str trimmed = p->trim();
int v = 0;
p->to_int(&v) catch e { return err(e.id, e.msg); };   /* 失败给 1..4 的码 */
p->copy_to(buf, sizeof buf);                   /* 空间不够返回 -1，不写半截 */
```

`S()` 存在的理由：裸字面量转 `char[:]` 会把结尾的 NUL 也算进长度
（`"hello"` 得到 6）。`S(lit)` 用 `sizeof(lit) - 1` 给出真实字符数。

切分与拼接（都只动视图，不分配）：

```c
foreach (Str tok = p->split(',')) { ... }    /* 空字段被跳过："a,,b" 给 a、b */
foreach (Str line = p->lines()) { ... }      /* 去掉 '\n'，CRLF 的 '\r' 也去掉 */
Str parts[2] = { { S("a") }, { S("bb") } };
join(parts, 2, '-', buf, sizeof buf);        /* "a-bb"；空间不够给 -1，不写半截 */
```

其余：`contains()` / `last_find()` / `last_find_char()` / `count_char()` /
`count_sub()` / `trim_left()` / `trim_right()` / `hash()`（FNV-1a，和 `std.map`
按字节求哈希是同一套）。大小写转换不能改原内存，所以写成
`copy_upper(dst, cap)` / `copy_lower(dst, cap)`：成功返回长度，空间不够给 -1。

### `Buf@(N)`：定长格式化

```c
import std.fmt;

Buf@(64) b = { { 0 }, 0 };               /* 纯值：char 数组 + 长度 */
Buf@(64) *o = &b;

o->write(S("n=")) catch e { return 1; };
o->write_int(-42) catch e { return 1; };
o->write_pad(7, 3, '0') catch e { return 1; };   /* "007" */
puts(o->cstr());                         /* 以 NUL 结尾 */
o->slice();                              /* 当前内容的切片 */
o->clear();  o->room();  o->cap();  o->count();
```

容量 N 里始终留一格给结尾 NUL：最多装 `N-1` 个字符。空间不够时返回
`T!error` 且**不写半截**（长度和内容都不动）。

### `Range`：能被 `foreach` 吃掉

```c
import std.range;

foreach (int i = range(0, 10)) { ... }          /* 0..9 */
foreach (int i = range_incl(1, 4)) { ... }      /* 1..4 */
foreach (int i = step_range(0, 10, 3)) { ... }  /* 0,3,6,9 */
foreach (int i = step_range(5, -1, -2)) { ... } /* 5,3,1 */
```

`foreach` 体必须是 `{ }` 块（协议在方法体里展开）。区间是纯值，可以按值
传参、手工 `(&r)->has()` / `(&r)->next()` 推进。

### 集成示例：`examples/wordcount.c6`

```sh
make std-demo
```

输出：

```
单词最多的行有 5 个单词
单词 12，数字 12，字符 61，最长单词 10
只取前两行：4 / 5 个单词
0..9 里有 5 个偶数
```

它同时用到 `std.types`、`std.err`、`std.str`、`std.vec`、`std.span`、
`std.fmt`、`std.range` 和自己写的模块 `wordcount.text`
（`examples/wordcount/text.c6`，说明模块边界怎么划、`-I` 怎么给）。`std.vec`
的排序内核来自 `std.sort`——`import` 只带声明，所以 `std/sort.c6` 的正文也
必须一起编（背后一共 8 个 `std` 模块）。

## 写法约定（照着写就不会踩坑）

* **普通结构体的方法就是「第一个参数是结构体指针」的文件作用域函数**：
  `int count(Str *self)`；泛型结构体才能在 `struct` 体内写方法体。
* 值容器调方法要 `(&v)->m()`；`@T` 托管的变量直接 `v->m()`。
* 泛型体内方法的隐式接收者叫 **`self`**（不是 `this`）。
* 方法名不能和字段同名（`int len;` + `int len()` → `field not found`）。
* `S()` 这类宏**不随 `import` 传递**：谁的代码里用，谁自己定义一次。
* `S("...")` 不能直接当初始化表达式，写成 `Str s = { S("hi") };`。
* 在 `T!error` 函数里转发错误用 `try f(...)`；`catch e { return e.id; }`
  会把 `e.id` 当成**成功**的返回值（`return` 只填值，不填错误槽）。
* `catch` 体必须以 `return`/`return err()`/`goto` 结束；想在普通函数里取出错误码，
  就 `catch e { id = e.id; goto done; }`。
* `foreach` 体必须是 `{ }`；`has()`/`next()` 必须带函数体。
* 泛型实参不要用块作用域里的 `struct` tag，把类型放到文件作用域。
* `#include` 要写在 `module` 前面；`main` 不能出现在模块里。

## 已知限制（编译器层面，标准库已绕开）

用标准库时可能碰到的、来自编译器而不是库本身的限制：

| 限制 | 现状 / 绕法 |
| --- | --- |
| 模板体内方法不能返回**多参数泛型实例**或**元组**（`Entry@(K,V) next()`、`(K,V) next()` → `declaration expected`） | 返回单参数类型或标量，多出来的东西走 `cur` 字段（`Map` 的迭代器就是这么做的），或者干脆不提供工厂方法 |
| 泛型**值形参**的模板写 `struct Buf@(int N)`、实例写 `Buf@(8)`（`Buf@(int, 8)` 会被当成两个实参） | 记住「声明带类型和名字，实例只给值」 |
| **普通**（非泛型）结构体不能在 `struct` 体内写方法体（`a function cannot be defined inside a struct`） | 方法写成「第一个参数是结构体指针」的文件作用域函数，调用写 `(&v)->m()` |
| 文件作用域里写「参数/返回类型带实例类型」的辅助函数，会让**导入这个模块的每个 TU** 都提前实例化该类型 | 别这么写；需要时把逻辑放进 `test` 块或用标量参数（`std/fmt.c6` 的测试用 `goto` 取错误码） |
| ~~往 `Buf@(N)` 里加若干形状相同的方法会挂~~（曾经的 `skip(')')` → `end_macro()` 死循环） | **已消失**：同形状方法（`char tmp[16]` + 进/移位 + `(char)` 强转）加到五个也编得过、跑得通，`std/fmt` 的 `write_u64`/`write_hex`/`write_bin`/`write_bool`/`write_ptr` 就是照这个形状补的（`tccpp.c` 的宏重放改写之后不再复现） |

这些限制在 `docs/grammar.md`（约束表 C1xx）和 `docs/plan.md` 里有对应的
条目；标准库的写法就是它们当前的「推荐用法」。

## 相关回归用例

* `tests/generic_scope.c6` + `tests/tu/generic_scope.c6`：模板实例的作用域
  边界（字段名 vs 同名方法、方法形参名不漏到调用方、两个 TU 都实例化同一
  模板时方法体是弱符号）。
* `make stdtest`：`std/` 下十三个模块的全部 `test` 块。
* `make std-demo`：上面那个集成示例。
