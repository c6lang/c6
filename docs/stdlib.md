# c6 标准库 `std/`

> **状态**：现行 API · **索引**：[文档索引](README.md) · **用法**：[manual/](manual/) · **诊断/号段**：[reference/grammar.md](reference/grammar.md)（§8.1）
>
> 一句话：`std/` 的设计原则、每个模块的 API、写法约定，以及编译器层面已知限制。

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
| `std/fmt.c6` | `std.fmt` | 定长格式化缓冲 `Buf@(N)`：`write_int`/`write_uint`/`write_u64`、`write_hex`/`write_hex64`/`write_bin`、`write_bool`/`write_ptr`、`write_pad`、拼接；数字转换原语 `digits_u32`/`digits_u64`/`digits_hex` 与 `std.strbuf` 共用 |
| `std/range.c6` | `std.range` | 能被 `foreach` 吃掉的区间 `Range`/`StepRange` |
| `std/sort.c6` | `std.sort` | 比较器驱动的排序与查找：`sort_bytes`/`sort_int`/`sort_u32`/`sort_f64`（稳定归并 + 小数组插入）、`find_bytes`/`find_int`/`find_u32`、`lower_bound_int`/`upper_bound_int`、`cmp_int`/`cmp_u32`/`cmp_f64`/`cmp_int_desc`、`is_sorted_int` |
| `std/mem.c6` | `std.mem` | 借用式内存池 `Arena`（`alloc`/`alloc_array`/`reset`/`room`/`used`）、托管堆薄包装 `alloc_new`/`alloc_new_array`/`dealloc`、按元素的 `copy`/`fill`/`zero`/`swap`/`rotate`/`reverse_bytes`、`align_up`/`align_down`/`is_aligned` |
| `std/io.c6` | `std.io` | 文件与标准流：`Reader`（`read_line`/`read_all`/`read_bytes`/`seek_to`/`tell`/`eof`/`close`）、`Writer`（`write_line`/`write_bytes`/`write_slice`/`flush`/`close`）、零分配的按行迭代 `CFile`（`has`/`next`）。方法名带 `reader_`/`writer_`/`cfile_` 前缀，调用照旧写 `pr->read_line(...)` |
| `std/math.c6` | `std.math` | 整数：`min`/`max`/`clamp`/`iabs`/`gcd`/`lcm`/`ipow`/`ipow_u64`/`imul_checked`/`sat_add`/`sat_mul`/`is_pow2`/`next_pow2`/`floor_div`/`mod`；浮点（不链 libm）：`sqrt`/`floor`/`ceil`/`trunc`/`round`/`fabs`/`nearly_eq`/`clamp_f64`/`fact` |
| `std/time.c6` | `std.time` | `now_ns`/`now_ms`/`now_unix`/`now_unix_ms`、`sleep_ms`/`sleep_ns`、`Duration` 拆分与 `(&d)->format(buf, cap)`（`duration_format`）、`Civil` 日历换算 `civil_from_unix`/`(&c)->to_unix()`（`civil_to_unix`）/`(&c)->format(...)`（`civil_format`），不依赖 localtime |
| `std/strbuf.c6` | `std.strbuf` | 会自己长大的字符缓冲 `StrBuf`（托管引用）：`write`/`write_cstr`/`push`、`write_int`/`write_u64`/`write_hex`、`reserve`/`truncate`/`clear`/`slice`/`cstr`（依赖 `std.fmt` 的数字原语） |
| `std/utf8.c6` | `std.utf8` | UTF-8 解码 `utf8_decode`（返回 1..4，拒绝过长编码/代理区/超范围/截断）、编码 `utf8_encode`、`utf8_valid`/`utf8_count`、`Rune` 与 `utf8_runes()` 迭代器 |
| `std/bits.c6` | `std.bits` | 位运算：`popcount32/64`、`parity32`、`clz32/64`、`ctz32/64`（0 输入给 32/64）、`rotl`/`rotr`、`bswap16/32/64`、`bit_get/set/clear/toggle`、`mask_low`/`mask_range`、`sign_extend`、`is_pow2_32/64` |
| `std/hash.c6` | `std.hash` | 哈希：FNV-1a 32/64（`fnv1a_bytes`/`fnv1a_cstr`/`fnv1a64_bytes`）、`crc32_bytes`、`adler32_bytes`，以及流式 `Hasher`（`hasher`/`update`/`digest`） |
| `std/path.c6` | `std.path` | POSIX 路径：`path_base`/`path_dir`/`path_ext`/`path_stem`/`path_is_abs`/`path_has_ext`/`path_split`、`path_join`/`path_normalize`（写调用方缓冲，放不下返回 -1 且不写半截） |
| `std/deque.c6` | `std.deque` | 环形缓冲双端队列 `Deque@T`（托管引用）与 `DIter@T`：`push_back`/`push_front`/`pop_back`/`pop_front`、`first`/`last`/`at`/`get`/`set`/`reserve`/`clear` |
| `std/ascii.c6` | `std.ascii` | ASCII 判别与转换（零分配、零依赖）：`ascii_is_digit`/`is_alpha`/`is_alnum`/`is_space`/`is_print`/`is_punct`/`is_cntrl`、`ascii_digit_value`/`hex_value`/`hex_char`、`ascii_to_lower`/`to_upper`、原地 `ascii_lower`/`upper`、`ascii_cmp_nocase`/`eq_nocase` |
| `std/num.c6` | `std.num` | 浮点格式化与解析（不链 libm）：`fmt_f64`（固定位）、`fmt_f64_sci`（`%e`）、`fmt_f64_g`（`%g` 语义、去尾零）、`round_to`、`f64_is_nan`/`f64_is_inf`、`parse_f64`/`parse_f64_cstr`（inf/nan、`INVAL`/`PARSE`/`RANGE`） |
| `std/codec.c6` | `std.codec` | 文本编码（写调用方缓冲）：Base64 `b64_encode`/`b64_decode`/`*_len`（接受省略填充与空白）、十六进制 `hex_encode`/`hex_decode`/`*_len`、百分号 `pct_encode`/`pct_decode`/`*_len`（`+` 可选） |
| `std/rand.c6` | `std.rand` | xorshift64* 伪随机：`rand_seed` + `(&r)->u64()`/`u32()`/`below()`（拒绝采样）/`int()`/`int_incl()`/`f64()`/`bool()`/`fill()`/`shuffle()`/`shuffle_int()`（方法名 `random_*`） |
| `std/fs.c6` | `std.fs` | POSIX 文件系统元数据与目录：`StatInfo`、`fs_stat`/`fs_lstat`/`fs_exists`/`fs_is_dir`/`fs_is_file`/`fs_size`、`fs_mkdir`/`fs_mkdir_p`/`fs_remove`/`fs_rmdir`/`fs_rename`、`fs_getcwd`/`fs_chdir`、零分配 `Dir` 遍历 |
| `std/set.c6` | `std.set` | 哈希集合 `Set@K`（`std.map` 的薄包装）与 `SetIter@K`：`add`（1 新增 / 0 已存在）/`remove`/`has`/`count`/`capacity`/`reserve`/`clear`/`iter` |
| `std/json.c6` | `std.json` | JSON（RFC 8259）解析与写出，DOM 住在 `std.mem` 的 `Arena` 上：`Json`（`JNULL`/`JBOOL`/`JNUM`/`JSTR`/`JARR`/`JOBJ`）、`json_parse`/`json_parse_at`、`json_get`/`json_at`/`json_count`/`json_key`、`json_get_str`/`json_get_num`/`json_get_bool`、`json_write`/`json_write_pretty`/`json_escape`/`json_unescape`（数字保留原文、`\uXXXX` 与代理对解码、深度上限 64） |
| `std/sha256.c6` | `std.sha256` | SHA-256 与 HMAC-SHA256（FIPS 180-4 / RFC 2104）：`Sha256` 的 `sha256_init`/`sha256_update`/`sha256_final`、一次性 `sha256`/`sha256_hex`、`hmac_sha256`/`hmac_sha256_hex`、常数时间 `sha256_eq` |
| `std/csv.c6` | `std.csv` | RFC 4180 CSV（零分配）：`CsvReader` 的 `csv_reader`/`csv_next`/`csv_rewind`（引号字段可跨行、CRLF、`""` 转义）、`csv_need_quote`/`csv_write_field`/`csv_write_row`（写调用方缓冲）、`csv_count_fields`/`csv_count_rows` |
| `std/env.c6` | `std.env` | 环境变量与命令行（POSIX）：`env_get`/`env_has`/`env_set`/`env_unset`、`env_home`/`env_tmpdir`/`env_path`/`env_user`、`env_get_int`/`env_get_u64`/`env_get_bool`、`env_args`/`env_argc`/`env_arg`/`env_prog` |
| `std/base64.c6` | `std.base64` | Base64（RFC 4648）标准表与 URL 安全表：`b64_encode`/`b64_decode`、`b64url_encode`/`b64url_decode`、显式表与填充的 `b64_encode_ex`/`b64_decode_ex`/`b64_decoded_len`/`b64_encoded_len`、`b64_valid`、就地 `b64_decode_inplace`/`b64url_decode_inplace`（跳过空白、严格填充、放不下不写半截） |
| `std/url.c6` | `std.url` | URL（RFC 3986）解析与相对解析：`Url` 的 `url_parse`/`url_effective_port`/`url_is_absolute`/`url_has_host`、`url_resolve`（§5.2 全算法）、查询参数 `url_next_pair`/`Pair`、百分号 `url_encode`/`url_encode_component`/`url_decode`/`url_decode_at`/`url_encoded_len`/`url_decoded_len` |
| `std/uuid.c6` | `std.uuid` | UUID（RFC 9562/4122）：`uuid_v4`/`uuid_v4_seeded`、`uuid_v7`/`uuid_v7_at`/`uuid_time_v7`、`uuid_v5` 与四个预定义命名空间 `uuid_ns_dns`/`uuid_ns_url`/`uuid_ns_oid`/`uuid_ns_x500`、`uuid_parse`/`uuid_format`/`uuid_format_simple`/`uuid_format_upper`、`uuid_version`/`uuid_variant`/`uuid_eq`/`uuid_nil`/`uuid_is_nil` |
| `std/mime.c6` | `std.mime` | 扩展名 ↔ MIME 类型、分类与魔数嗅探（零分配）：`mime_of_ext`/`mime_of_path`/`mime_ext_of`/`mime_ext_at`/`mime_type_at`/`mime_count`、`mime_is_text`/`is_image`/`is_audio`/`is_video`/`is_font`、`mime_charset`、`mime_sniff`、`mime_default` |
| `std/inet.c6` | `std.inet` | IP 地址与 CIDR（零分配）：`inet_parse`/`inet_format`、`inet_parse_v4`/`inet_format_v4`、`inet_parse_v6`/`inet_format_v6`（输出按 RFC 5952 压缩）、`inet_parse_cidr`/`inet_cidr_contains`/`inet_cidr_mask`/`inet_v4_mask`、`inet_is_private`/`is_loopback`/`is_link_local`/`is_multicast`/`is_unspecified`、`inet_parse_port` |
| `std/bitbuf.c6` | `std.bitbuf` | 位级读写器（零分配、MSB 先）：`bw_open`/`bw_put`/`bw_put_u64`/`bw_put_bit`/`bw_align`/`bw_put_bytes`/`bw_patch`（回填不改游标）、`br_open`/`br_get`/`br_get_u64`/`br_get_bit`/`br_peek`/`br_skip`/`br_align`/`br_get_bytes`、`bw_bits`/`bw_bytes`/`bw_ok`/`br_remaining`/`br_eof` |
| `std/pqueue.c6` | `std.pqueue` | 二叉堆 `PQueue@T`（托管容器）：`push`/`pop`/`peek`/`push_pop`/`heapify`/`clear`、`count`/`capacity`/`is_empty`/`is_valid`、`init`/`set_cmp`/`set_max`（比较器与 `std.sort` 的 `cmp_int`/`cmp_str` 通用） |
| `std/ini.c6` | `std.ini` | INI 配置的零分配读取：`ini_open`/`ini_next`（字段都是输入上的视图）、`ini_lookup`/`ini_has_section`/`ini_count`、`IniEntry.section`/`key`/`value`/`line`；语法错误给行号 |
| `examples/wordcount.c6` | （无模块） | 把上面几块拼起来的示例程序：入口 `main` 只能写在**不属于任何模块**的文件里 |
| `examples/stdtour.c6` | （无模块） | 新模块的拼装示例（`make std-tour`）：strbuf/path/hash/bits/utf8/deque 各一节，自带自检 |
| `examples/stdkit.c6` | （无模块） | 第三批模块的拼装示例（`make std-kit`）：ascii/num/codec/rand/fs/set 各一节，自带自检 |
| `examples/stdbox.c6` | （无模块） | 第四批模块的拼装示例（`make std-box`）：json/sha256/csv/env 各一节，自带自检 |
| `examples/stdweb.c6` | （无模块） | 第五批模块的拼装示例（`make std-web`）：base64/url/uuid/mime 各一节，自带自检 |
| `examples/stdnet.c6` | （无模块） | 第六批模块的拼装示例（`make std-net`）：inet/bitbuf/pqueue/ini 各一节，自带自检 |

每个 `std/*.c6` 都自带 `test` 块，`make stdtest` 逐个跑，输出形如（数字会随模块增加变化，
以实际输出为准）：

```
std/str.c6       22 test(s), 0 failure(s)
std/sort.c6      30 test(s), 0 failure(s)
std/ascii.c6      5 test(s), 0 failure(s)
std/base64.c6     8 test(s), 0 failure(s)
std/bitbuf.c6     7 test(s), 0 failure(s)
std/bits.c6       6 test(s), 0 failure(s)
std/codec.c6      7 test(s), 0 failure(s)
std/csv.c6       41 test(s), 0 failure(s)
std/deque.c6      9 test(s), 0 failure(s)
std/env.c6       39 test(s), 0 failure(s)
std/err.c6        3 test(s), 0 failure(s)
std/fmt.c6       10 test(s), 0 failure(s)
std/fs.c6         7 test(s), 0 failure(s)
std/hash.c6       7 test(s), 0 failure(s)
std/inet.c6       8 test(s), 0 failure(s)
std/ini.c6        6 test(s), 0 failure(s)
std/io.c6         5 test(s), 0 failure(s)
std/json.c6      60 test(s), 0 failure(s)
std/map.c6        9 test(s), 0 failure(s)
std/math.c6       6 test(s), 0 failure(s)
std/mem.c6        7 test(s), 0 failure(s)
std/mime.c6      37 test(s), 0 failure(s)
std/num.c6       11 test(s), 0 failure(s)
std/path.c6      30 test(s), 0 failure(s)
std/pqueue.c6    38 test(s), 0 failure(s)
std/rand.c6       9 test(s), 0 failure(s)
std/range.c6      6 test(s), 0 failure(s)
std/set.c6       17 test(s), 0 failure(s)
std/sha256.c6    23 test(s), 0 failure(s)
std/span.c6       6 test(s), 0 failure(s)
std/strbuf.c6    18 test(s), 0 failure(s)
std/time.c6       7 test(s), 0 failure(s)
std/types.c6      4 test(s), 0 failure(s)
std/url.c6       32 test(s), 0 failure(s)
std/utf8.c6       7 test(s), 0 failure(s)
std/uuid.c6      24 test(s), 0 failure(s)
std/vec.c6       42 test(s), 0 failure(s)
```

`-test` 的计数是命令行上**所有 TU** 的 `test` 块之和，所以带依赖跑的行会把自己
和依赖的块一起算进去，于是比模块本身的块数多：

| 行 | 构成 |
| --- | --- |
| `std/sort.c6` 30 | str 22 + sort 8 |
| `std/vec.c6` 42 | str 22 + sort 8 + vec 12 |
| `std/path.c6` 30 | str 22 + path 8（path 只借 `Str` 这个类型，函数体不依赖 str 的代码） |
| `std/strbuf.c6` 18 | fmt 10 + strbuf 8 |
| `std/set.c6` 17 | map 9 + set 8 |
| `std/json.c6` 60 | json 17 + str 22 + num 11 + mem 7 + err 3 |
| `std/csv.c6` 41 | csv 19 + str 22（csv 只借 `Str` 类型，函数体不依赖 str 的代码） |
| `std/env.c6` 39 | env 14 + str 22 + err 3（env 用到 `str()`） |
| `std/sha256.c6` 23 | sha256 17 + bits 6（`rotr32` 是跨 TU 调用） |
| `std/url.c6` 32 | url 10 + str 22（url 只借 `Str` 类型，正文不调用 str） |
| `std/mime.c6` 37 | mime 7 + str 22 + path 8（取扩展名要 `std.path` 的正文） |
| `std/uuid.c6` 24 | uuid 8 + rand 9 + time 7（v4/v7 要随机源，v7 要墙钟） |
| `std/pqueue.c6` 38 | pqueue 8 + sort 30（比较器来自 `std.sort`；`cmp_str` 又拉上 `std.str`，但那样跑才有 str 的块参与） |

`std/bits`、`std/hash`、`std/utf8`、`std/deque`、`std/ascii`、`std/num`、
`std/codec`、`std/rand`、`std/fs`、`std/base64`、`std/inet`、`std/bitbuf`
自成一体，单独跑就是自己的块数；`std/csv`、`std/url`、`std/ini` 只有声明依赖
`std.str`（正文不调用），`std/mime` 的正文必须带 `std/path`，`std/sha256` 的正文
必须带 `std/bits`，`std/env` 的正文必须带 `std/str`，`std/uuid` 的正文必须带
`std/rand` 与 `std/time`，`std/pqueue` 的正文必须带 `std/sort`。

## 设计原则

**1. 值语义优先，分配只发生在明确的地方。**
`Str`、`Span@T`、`Buf@(N)`、`Range` 都是纯值：没有指针、没有托管字段，
可以按值声明、按值传参、放进别的结构里，栈上就够。要「会自己长大」的
东西（`Vect@T`、`Map@(K, V)`）才用托管引用 `@T` + `new`——它们的生命周期
和释放由引用计数来管，调用方不用写 `free`。

**2. 不分配的东西不该失败，会失败的写 `T!error`。**
`span->at(i)` 越界返回 `0`/不写；`vec->push(v)` 可能没有内存，于是返回
`int!error`，调用方必须 `try` 或 `catch` 消费（编译器强制，见
`docs/reference/grammar.md` C15）。这条分工让「几乎不会失败」的路径保持零开销，
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
哈希与撞号检查；std 模块的号段表见 [grammar.md](reference/grammar.md) §8.1。

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

扩容重建时**只搬 `SLOT_LIVE` 的条目**：墓碑是 `SLOT_DEAD`（值为 2），把它写成
`if (!of[i]) continue;` 会把死键连同旧值一起「复活」，重建之后 `count` 少一、
已删除的键又出现。这个坑是写 `std/set` 时踩到的（Set 的 add/remove 会留下墓碑，
再插入就触发扩容），`std/map.c6` 的用例「墓碑不会在扩容时复活」钉住了它。

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

解析与剥离（都只动视图）：

```c
u64 n = 0ull;
p->to_u64(&n) catch e { return err(e.id, e.msg); };  /* 1 空 2 非数字 3 越界 4 没数字 */
Str rest = { S("") };
if (p->strip_prefix(S("src/"), &rest)) { ... }       /* 命中给 1，没命中不动 *rest */
p->strip_suffix(S(".c6"), &rest);                     /* 对称 */
foreach (Str w = p->fields()) { ... }                 /* 按空白切分，连续空白只算一个 */
```

`to_u64` 先判 `(U64_MAX - d) / 10` 再乘，所以溢出不会悄悄回绕；`-1` 报码 2
（无符号不接受负号），`+` 前缀可以。`fields()` 跳过空字段，`"  a  b "` 给
`a`、`b`。

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

数字转换的脏活被抽成了三个文件作用域原语：`digits_u32`、`digits_u64`、
`digits_hex`——往调用方给的缓冲里写十进制/十六进制，返回长度，放不下返回
`-1` 且一个字节都不写。`Buf@(N)` 的 `write_*` 和 `std.strbuf` 都建在它们
上面，所以两条路径的格式永远一致。`write_hex` 收 `u32`，要写满 64 位十六进制
用 `write_hex64`（两个入口都只是 `digits_hex` 的薄包装）。曾经把 `write_hex`
直接加宽到 `u64` 会挂住编译器，现在**复现不出来**了（见下面的「已知限制」），
保留一个 `u32` 入口只是不想为改签名动 API。

### `StrBuf`：会自己长大的字符串

```c
import std.strbuf;

@StrBuf b = new StrBuf;                  /* 托管引用：含 @char 字段，必须 new */
b->write(S("n=")) catch e { return 1; }; /* 失败不改内容 */
b->write_int(-42) catch e { return 1; };
b->write_hex(0xdeadbeefu) catch e { return 1; };
b->push('\n') catch e { return 1; };
puts(b->cstr());                         /* 总是以 NUL 结尾 */
b->count();  b->capacity();  b->room();  b->is_empty();
b->slice();                              /* 当前内容的切片 */
b->truncate(3);  b->clear();             /* 只改长度，容量留着 */
b->reserve(4096) catch e { return 1; };  /* 翻倍策略写在库里 */

/* 方法名是 strbuf_write / strbuf_count / strbuf_cstr …，上面这些写法都对：
   普通结构体（体内写不了方法体）的方法按 strbuf_* 注册，符号不与别的模块抢 */
```

和 `Buf@(N)` 的分工：上限是常量（协议头、日志行）就用 `Buf@(N)`，纯值、零
分配；长度事先不知道才用 `StrBuf`。扩容从 64 起翻倍，超过 `0x3fffffff` 直接
报 `NOMEM`（不先分配再失败）。

### `path_*`：POSIX 路径拆装

```c
import std.path;
import std.str;                          /* 只为 Str 这个类型 */

Str p = { S("docs/stdlib.md") };
Str d = path_dir(p.s);                   /* "docs" */
Str b = path_base(p.s);                  /* "stdlib.md" */
Str e = path_ext(p.s);                   /* ".md" */
Str s = path_stem(p.s);                  /* "stdlib" */

char buf[64] = { 0 };
path_join(d.s, b.s, buf, sizeof buf);    /* 14，"docs/stdlib.md" */
path_normalize(S("./docs//stdlib.md"), buf, sizeof buf);
```

规则：只认 `/`（没有盘符），尾部斜杠先删；`dir` 没有斜杠时给 `"."`（POSIX
`dirname` 的约定，配合 `join` 的特判能还原原路径）；`.bashrc`、`b.` 都算
没有扩展名。`path_normalize` 折叠重复斜杠和 `"."`，但**保留 `".."`**——词法
消 `".."` 会穿过符号链接改变语义，这个决定不能由库替调用方做。所有函数都
不分配：拆出来的 `Str` 是指向调用方内存的视图，拼装写进调用方的缓冲，放不
下返回 `-1` 且不写半截。

### `hash` / `bits` / `utf8`：三块零分配的底层

```c
import std.hash;
import std.bits;
import std.utf8;

u32 h = fnv1a_cstr("docs/stdlib.md");    /* 与 Str::hash、std.map 同算法 */
u32 c = crc32_bytes("123456789", 9);     /* 0xCBF43926 */
u64 h64 = fnv1a64_bytes(p, n);

Hasher st = hasher();                    /* 流式：分几段喂结果一样 */
(&st)->update(s1.data, (usize)s1.len);   /* = hasher_update(&st, ...) */
u32 d = (&st)->digest();                 /* = hasher_digest(&st) */

popcount32(0xf0f0f0f0u);  clz32(1u);  ctz32(8u);   /* 0 输入给 32/64 */
rotl32(v, 1);  bswap32(v);  mask_range(4, 8);
is_pow2_32(1024u);

u32 cp = 0;
int n2 = utf8_decode(S("héllo"), 1, &cp);          /* 2，cp = 0xe9 */
utf8_encode(0x4e16, buf, sizeof buf);              /* 3 字节；非法码点给 -1 */
utf8_valid(S("..."));  utf8_count(S("..."));       /* 校验 / rune 计数 */
foreach (Rune r = utf8_runes(S("héllo"))) { ... }  /* r.cp / r.pos / r.size / r.ok */
```

`utf8_decode` 返回 1..4（占用字节数），拒绝过长编码、代理区、`> U+10FFFF`
和截断序列，坏字节只前进 1 个字节；`utf8_encode` 先校验码点，失败时**不写
任何内容**。`crc32` 是逐位实现（没有 256 项的表），`adler32` 也是纯算术，
两者都不链 `zlib`。

### `Deque@T`：环形缓冲双端队列

```c
import std.deque;

@Deque@int d = new Deque@int;
d->push_back(2) catch e { return e.id; };
d->push_front(1) catch e { return e.id; };
int v = 0;
d->pop_front(&v);                        /* 1 */
d->pop_back(&v);                         /* 2 */
d->at(0);  d->get(0, &v);  d->set(0, 7);
d->first(&v);  d->last(&v);
foreach (int x = d->iter()) { ... }
```

元素放在 `@T` 托管的环形数组里，逻辑下标是 `(head + i) % cap`；扩容后按逻辑
顺序摊平（`head = 0`）。和 `Vect` 的分工：要连续内存（排序、切片、按字节搬）
用 `Vect`，要两端进出用 `Deque`——`push_front` 在数组上只能是 O(n)，环形下标
才是它存在的理由。`push_*`/`reserve` 会分配，返回 `int!error`；`pop_*` 空容器
给 0 且不写 `*out`；`at(i)` 是前置条件，要检查就用 `get`。

### `ascii`：字节判别与大小写

```c
import std.ascii;

ascii_is_digit('7');  ascii_is_alpha('A');  ascii_is_space('\t');
ascii_hex_value('F');            /* 15；非法给 -1 */
ascii_to_lower('Q');             /* 'q' */
ascii_cmp_nocase(S("Hello"), S("hello"));   /* 0 */

char buf[8] = "aBc";
ascii_upper(buf, 3);             /* 原地 -> "ABC" */
```

参数收 `int` 而不是 `char`：`fgetc()` 的 EOF(-1) 和带符号 char 的负值都安全，
`c < 0 || c > 127` 一律判为「不是」，转换函数原样返回。只认 ASCII、不看
locale——多字节是 `std.utf8` 的事。`ascii_upper`/`ascii_lower` 是这一层唯一的
「写」函数，原地改调用方缓冲；要不改原内存的大小写转换用 `std.str` 的
`copy_upper`/`copy_lower`。

### `num`：浮点的格式化与解析

```c
import std.num;

char buf[64] = { 0 };
fmt_f64(3.14159, buf, sizeof buf, 2);        /* "3.14" */
fmt_f64_sci(12345.678, buf, sizeof buf, 2);  /* "1.23e+04" */
fmt_f64_g(1.0, buf, sizeof buf, 6);          /* "1"（%g 去掉尾零） */
fmt_f64_g(0.00001234, buf, sizeof buf, 6);   /* "1.234e-05" */

f64 v = 0.0;
parse_f64(S("3.25"), &v) catch e { return e.id; };
```

这一层是给「没有 printf 的 freestanding 目标」和「不想依赖 libc 浮点格式」
的场合准备的，语义全部钉死：

* `fmt_f64` 是固定小数位（`prec` 0..9，超出夹住），`|x| >= 1e18` 返回 -1；
* `fmt_f64_sci` 是 `d.dddde±XX`，指数至少两位，和 C 的 `%e` 一致；
* `fmt_f64_g` 在「指数 < -4 或 >= 有效位数」时用科学计数，否则固定，并去掉
  尾零：`1.0` → `"1"`、`100.0` → `"100"`；
* 三个函数都是**四舍五入（.5 远离零）**；二进制表示本身的误差照实反映
  （`1.005` 实际略小于 1.005，所以两位给 `"1.00"`）；
* 放不下返回 -1 且一个字节都不写；`nan`/`inf`/`-inf` 是固定拼写；
* `parse_f64` 空/全空白 → `INVAL`、语法错 → `PARSE`、溢出 → `RANGE`，下溢
  给 0；`inf`/`infinity`/`nan` 大小写不敏感。它不是「正确舍入」实现：最多
  累积 19 位有效数字，常见输入（15 位以内）与 libc 一致。

### `codec`：Base64 / 十六进制 / 百分号

```c
import std.codec;

char b64[32] = { 0 };
b64_encode(S("foobar"), b64, sizeof b64);        /* 8，"Zm9vYmFy" */
b64_decode(S("Zm9vYmFy"), buf, sizeof buf);      /* 6，"foobar" */

char hex[16] = { 0 };
hex_encode(S("\xde\xad"), hex, sizeof hex, 1);   /* 4，"DEAD" */
hex_decode(S("DEAD"), out, sizeof out);          /* 2 */

char pct[32] = { 0 };
pct_encode(S("a b/c"), pct, sizeof pct, (char *)"/");  /* 7，"a%20b/c" */
pct_decode(S("a+b"), out, sizeof out, 1);        /* plus -> 空格 */
```

和 `std.path`/`std.fmt` 同一个约定：**不分配**，写调用方缓冲，放不下返回 -1
且一个字节都不写。解码器先完整校验一遍再写第二遍，非法输入不会留下半截：
Base64 接受省略填充和 ASCII 空白，拒绝 `% 4 == 1`、非法字符、`=` 在中间；
十六进制要求偶数个字符、不跳空白；百分号拒绝截断的 `%X`。`*_encoded_len`/
`*_decoded_len` 可以先问长度。

### `rand`：确定性伪随机

```c
import std.rand;
import std.time;

Rand r = rand_seed(now_ns());           /* 纯值，可重放：同一个种子同一串数 */
u64 x = (&r)->u64();                    /* = random_u64(&r) */
int d6 = (&r)->int_incl(1, 6);          /* [1, 6] */
u64 b = (&r)->below(100);               /* [0, 100)，拒绝采样，无余数偏置 */
f64 p = (&r)->f64();                    /* [0, 1) */

int a[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
(&r)->shuffle_int((int[:]){ a, 10 });
```

算法是 xorshift64*（只用 u64 的异或/移位/乘），种子 0 换成非零常量（全零状态
是吸收态）。**不是密码学随机数**：密钥、令牌、盐不要用它。`shuffle` 按
字节交换，任意元素宽度都能用（`elem` 给 `sizeof`）。已知序列钉在
`std/rand.c6` 的 `test` 块里。

### `fs`：文件系统元数据与目录

```c
import std.fs;

StatInfo st = { 0 };
fs_stat("docs/stdlib.md", &st) catch e { return e.id; };
st.is_file;  st.size;  st.mtime;

fs_mkdir_p("build/out/a", 0755) catch e { return e.id; };

Dir d = dir_open("std");
Dir *pd = &d;
char name[256] = { 0 };
int is_dir = 0;
while (pd->dir_next(name, sizeof name, &is_dir) > 0) { ... }
pd->dir_close();
```

`std.io` 管内容、`std.fs` 管元数据和目录。`errno` 被翻译成 `std.err` 的码：
不存在 → `NO_ENT`、已存在 → `EXIST`、参数/不是目录/没权限 → `INVAL`、其余 →
`IO`。`dir_next` 返回 1 取到、0 结束、-1 出错、**-2 名字比 cap 长**（不截断，
让调用方加大 buffer）；"."/".." 照常返回。`is_dir` 取自 `dirent.d_type`，
`DT_UNKNOWN` 时给 0。这一层依赖 POSIX，freestanding 目标用不了（io 至少只要
stdio）。

### `set`：哈希集合

```c
import std.set;

@Set@int s = new Set@int;
s->add(7) catch e { return e.id; };   /* 返回值：1 本次新增、0 已经存在 */
s->has(7);  s->remove(7);  s->count();
foreach (int k = s->iter()) { ... }
```

键的要求和 `std.map` 完全一致（按 `sizeof(K)` 个字节做 FNV-1a、memcmp 判等），
`Set` 就是 `@Map@(K, int)` 的薄包装——所以**用 Set 的程序要把 `std/map.c6` 的
正文一起编**。`add` 会分配、返回 `int!error`（失败不改集合）；`remove`/`has`
不失败；`iter()` 借用集合，改集合之前先迭代完。

### `json`：arena 上的 DOM

```c
import std.json;
import std.mem;
import std.str;

#define S(lit) ((char[:]){ (char *)(lit), (int)(sizeof(lit) - 1) })

char pool[4096] = { 0 };                     /* DOM 住在这块内存里，不 malloc */
Arena a = arena_make(pool, (int)sizeof pool);
Str text = { S("{\"n\": 3, \"ok\": true}") };  /* char[:] 不会隐式转成 Str */
Json root = json_parse(&a, text) catch e { return e.id; };
Json *p = &root;

Json *n = p->json_get("n");                  /* 找不到 / 不是对象给 0 */
int ok = p->json_get_bool("ok", 0);
f64 v = p->json_get_num("n", 0.0);
char out[64] = { 0 };
p->json_write(out, sizeof out);               /* {"n":3,"ok":true} */
```

`Json` 是纯值结构体，解析出来的整棵树都由传进去的 `Arena` 分配：`reset` 一次
全部释放，不碰 `malloc`、不引入托管引用（所以 `std.json` 在 freestanding 目标
也能用，只要 `std.mem` 的 alloc 还在）。字符串值解码进 arena，键和值都是指向
arena 的 `Str` 视图；`json_parse` 失败时消息是静态描述，要定位就换
`json_parse_at`（多一个 `int *err_pos`，写字节偏移）。

语义钉死的几处：

* **严格 RFC 8259**：数字只认 `-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?`
  （`+1`、`.5`、`01`、`1.`、`nan` 都拒绝）；字符串里未转义的控制字符报
  `PARSE`；`\uXXXX` 解码成 UTF-8，代理对合并、落单代理报 `PARSE`；顶层任意
  值；允许开头一个 UTF-8 BOM；尾随垃圾报 `PARSE`。
* **数字不丢精度**：`JNUM` 同时保留 `raw`（输入原文）与 `num`（f64）。写出时
  优先 `raw`，所以 `9007199254740993` 原样往返；没有 `raw` 的（用户自己构造的
  节点）用 `std.num` 的 `fmt_f64_g` 现算（有效位数按十进制指数收敛到 11–17 位，
  `inf`/`nan` 写成 `null`，因为 JSON 没有这两个字面量）。
* **递归上限 64 层**，超过给 `RANGE`；空输入/只有空白给 `INVAL`；arena 满给
  `NOMEM`。
* `json_get` 对重复键返回**最后**一个；`json_count`/`json_at`/`json_key` 只在
  对应容器上有效。
* 写函数都是「先算长度再写」：容量不够返回 -1 且一个字节都不写；
  `json_write_pretty(..., indent)` 每层缩进 `indent` 个空格（`<= 0` 当 2）。

### `sha256`

```c
import std.sha256;

u8 dig[32] = { 0 };
char hex[65] = { 0 };
Sha256 st = { { 0 }, 0, { 0 }, 0 };

sha256("abc", 3, dig);                       /* 一次性 */
sha256_hex("abc", 3, hex);                   /* "ba7816bf…15ad"，小写 + NUL */

sha256_init(&st);                            /* 流式：任意切分结果相同 */
sha256_update(&st, "a", 1);
sha256_update(&st, "bc", 2);
sha256_final(&st, dig);

hmac_sha256(key, klen, msg, mlen, dig);      /* RFC 2104 */
sha256_eq(dig, other32);                     /* 常数时间比较，1/0 */
```

FIPS 180-4 的纯实现：只用 `memcpy`/`memset`，不分配、不链 libm、不碰宿主加密
库。`sha256_update` 支持 1 字节一喂和任意块边界（63/64/65 都测过），
`sha256_final` 之后想复用就再 `init`。HMAC 的 key 超过 64 字节先哈希，ipad/opad
在栈上算。已知向量（空串、`"abc"`、两块的 NIST 串、一百万 `a`，以及 RFC 4231
的 HMAC 用例 1/2/6）全部钉在 `std/sha256.c6` 的 `test` 块里。

### `csv`

```c
import std.csv;
import std.str;

Str text = { S("name,qty\nbolt,12\n\"M6, hex\",3\n") };
CsvReader r = csv_reader(text);               /* 纯值：按值声明、按值传 */
CsvReader *p = &r;
Str f = { { (char *)0, 0 } };

while (csv_next(p, &f) > 0) {                 /* 1 有字段、0 结束、-1 格式错 */
    /* f 是输入里的视图；p->row / p->col / p->at_eol 告诉你位置 */
    if (p->at_eol)
        putchar('\n');
}
if (p->error)
    printf("csv: %d\n", p->error);             /* 本模块的码，不是 std.err 的 */

char line[64] = { 0 };
Str row[2] = { { S("M6, hex") }, { S("3") } };
csv_write_row(row, 2, line, sizeof line, 1);  /* "M6, hex",3 + CRLF */
```

零分配读取器 + 写调用方缓冲，规则是 RFC 4180 加一点宽容：`"` 开头的字段可以
跨行（`\n` 合法），内部的 `""` 表示一个引号；引号字段结束后只允许 `,`、行尾或
EOF，否则 `PARSE`；行分隔符 `\n`/`\r\n`/`\r` 都认；空输入立刻结束；空行是
**一条记录、一个空字段**（RFC 的语义），末尾换行不再多造一条记录；未加引号的
字段里出现 `"` 按普通字节处理。

视图给的是「引号内的原始字节」：`"a""b"` 给 4 字节 `a""b`，因为把 `""` 折成
一个 `"` 要改长度、只能复制。要真值就展开到自己的缓冲（`csv_unquote`，长度先问
`csv_unquoted_len`）；不含 `""` 的字段——绝大多数——视图就是真值。
`csv_count_fields`/`csv_count_rows` 用同一套规则数数，格式错返回 -1。写侧只有
「含 `,`/`"`/CR/LF」才加引号，容量不够返回 -1 且不写半截。

`p->error` 里存的是**本模块**的码（`std.csv.PARSE` / `std.csv.INVAL`），不是
`std.err` 的码，所以别直接丢给 `std.err.msg()`。

### `env`：环境变量与命令行

```c
import std.env;

int main(int argc, char **argv)
{
    env_args(argc, argv);                     /* 入口喂一次，之后都能用 */
    printf("%d 个参数，程序名 %.*s\n", env_argc(),
           env_prog().s.len, env_prog().s.data);

    Str home = env_home();                    /* HOME，缺省 "" */
    Str tmp = env_tmpdir();                   /* TMPDIR，缺省 "/tmp" */
    int port = env_get_int("PORT", 8080);     /* 不存在/语法错给默认值 */
    int debug = env_get_bool("DEBUG", 0);     /* 1/true/yes/on */

    env_set("MODE", "dev") catch e { return e.id; };
    env_unset("MODE") catch e { return e.id; };
    ...
}
```

`import` 只带来声明，模块拿不到真实 `argc/argv`，所以入口要调一次
`env_args`；`env_arg(i)` 越界给空串。`env_get`/`env_home` 这些返回的是
`environ` 里的**视图**（不复制、不分配），下一次 `env_set`/`env_unset` 之后
就失效，要留住就自己拷走。这一层依赖 POSIX 的 `setenv`/`environ`，
freestanding 目标用不了。数值解析是严格的十进制（可选正负号、忽略前后空白、
溢出给默认值），布尔认 `1/true/yes/on` 与 `0/false/no/off`（大小写不敏感）。

### `base64`：标准表与 URL 安全表

```c
import std.base64;

char enc[64] = { 0 };
char dec[64] = { 0 };

b64_encode(S("foobar"), enc, sizeof enc);        /* 8，"Zm9vYmFy" */
b64url_encode(S("\xfb\xff"), enc, sizeof enc);   /* 3，"-_8"（不带填充） */
b64_decode(S("Zm9vYmFy"), dec, sizeof dec);      /* 6，"foobar" */
b64url_decode(S("-_8"), dec, sizeof dec);        /* 2 */

b64_encoded_len(6, 1);                           /* 8：先问长度再给缓冲 */
b64_decoded_len(S("Zm9vYmFy"), 0, 1);            /* 6；非法输入给 -1 */
```

`std.codec` 里那份 Base64 是**宽容**口径（跳过空白、接受省略填充）。这一层是给
协议实现用的，表和填充都是**显式参数**，因为 JWT/JWKS 与 HTTP 头的要求不一样：

* `url` 参数：0 = 标准表（`+` `/`），1 = URL 安全表（`-` `_`）；两张表**互不混用**；
* `pad` 参数：1 = 输出填充 / 要求填充，0 = 不输出 / 不要求。

```c
b64_encode_ex(S("f"), enc, sizeof enc, 0, 0);    /* 2，"Zg"——不带 `=` */
b64_decode_ex(S("Zg"), dec, sizeof dec, 0, 0);   /* 1：pad = 0 时才接受省略 */
b64_decode(S("Zg"), dec, sizeof dec);            /* -1：pad = 1 缺填充就报错 */
b64url_decode(S("Zg=="), dec, sizeof dec);       /* -1：URL 变体下 `=` 是非法字符 */
```

**编码不写结尾 NUL**（和 `std.codec` 一致，长度就是返回值）；要当 C 字符串用就多给
一格。`*_decode_inplace` 是例外：它在同一块内存上解码，**会补一个 NUL**（否则输入
剩下的编码字符会留在结果尾巴上）。解码一律先完整校验再写第二遍，非法输入与「放
不下」都不留半截结果。

### `url`：RFC 3986 的分解、相对解析与百分号

```c
import std.url;

Url u = url_parse(S("https://user:pw@example.com:8443/a/b?q=1&r=2#frag"));
u.ok;  u.scheme;  u.userinfo;  u.host;  u.port;  u.path;  u.query;  u.fragment;
url_effective_port(&u);            /* 8443；没写端口时按 scheme 给 80/443/21/23 */

char out[256] = { 0 };
url_resolve(S("http://a/b/c/d;p?q"), S("../g"), out, sizeof out);   /* "http://a/b/g" */

Pair p = { { { (char *)0, 0 } }, { { (char *)0, 0 } } };
int at = 0;
while (url_next_pair(&u.query, &at, &p))
    printf("%.*s = %.*s\n", p.key.s.len, p.key.s.data,
           p.value.s.len, p.value.s.data);
```

`Url` 的每个字段都是**指向输入串的视图**（不分配），没出现的组件是 `{ 0, 0 }`。
所以「空 query（`?`）」与「没有 query」在视图层面合并——不做 URL 规范化比较。

* `url_parse` 按 §3 分解：scheme 必须字母开头且全由字母/数字/`+`-`.` 组成；
  authority 里**最后一个** `@` 才是 userinfo 的边界；`[::1]` 里的冒号不算端口
  分隔符；端口只认十进制且 ≤ 65535，非法时 `ok = 0` 但**保留原文**；
* `url_resolve` 是 §5.2 的完整算法（`remove_dot_segments` 逐条照抄），RFC §5.4 的
  23 条 normal + abnormal 用例全钉在自测里。结果先在栈上拼好，**最后一步才写
  `dst`**，所以「放不下」返回 -1 时调用方缓冲一个字节都没动；
* `url_encode` 保留 `/`（整条路径），`url_encode_component` 连 `/` 也编（单个参数）；
  编码与 `url_decode` 都**不写结尾 NUL**；解码时**非法转义原样保留**（URL 里的 `%`
  不总是转义），`+` 要不要当空格由 `url_decode_at(..., plus)` 显式指定；
* `Url u = { 0 };` 这种全零初始化是支持的：「有没有显式端口」看 `port_str`，不看
  `port >= 0`。

### `uuid`：v4 / v5 / v7

```c
import std.uuid;

Uuid a = uuid_v4();                            /* 随机 */
Uuid b = uuid_v7();                            /* 时间排序：前 48 位是 Unix 毫秒 */
Uuid ns = uuid_ns_dns();
Uuid c = uuid_v5(&ns, S("www.example.com"));   /* 2ed6657d-e927-568b-95e1-2665a8aea6a2 */

char buf[40] = { 0 };
(&a)->format(buf, sizeof buf);                 /* 36 字符 + NUL */
uuid_format_simple(&a, buf, sizeof buf);       /* 32 字符，不带连字符 */
Uuid back = { { 0 } };
uuid_parse(S("6ba7b810-9dad-11d1-80b4-00c04fd430c8"), &back);   /* 1 */

uuid_version(&c);      /* 5 */
uuid_variant(&c);      /* 1 = RFC 4122 */
uuid_time_v7(&b);      /* 版本 7 里那 48 位毫秒时间戳 */
```

`Uuid` 是 16 字节的**纯值**（`struct Uuid { u8 b[16]; }`），可放进别的结构体、按
字节比较。三个生成器的取舍：

| 生成器 | 随机源 | 用途 |
| --- | --- | --- |
| `uuid_v4` | 时钟 + 栈地址播种的 `std.rand` | 通用唯一标识；**不是密码学随机** |
| `uuid_v7` | Unix 毫秒 + 随机位 | 数据库主键之类「想按时间聚簇」的场合（同毫秒内不保证严格单调） |
| `uuid_v5` | 无（SHA-1 散列） | 同一命名空间 + 名字永远给同一个 UUID；SHA-1 只在文件内部实现，不链加密库 |

要可重现的用例用 `uuid_v4_seeded(&r)` / `uuid_v7_at(ms, &r)`。`uuid_parse` 接受
36 字符、32 字符、带 `urn:uuid:` 前缀与 `{}` 包装的形式，大小写不敏感；坏输入返回 0
且**不动 `*out`**。

### `mime`：扩展名 ↔ 类型、分类与魔数

```c
import std.mime;

Str t = mime_of_path(S("site/index.HTML"));   /* "text/html"（大小写不敏感） */
Str u = mime_of_ext(S(".png"));               /* "image/png"，带不带点都收 */
Str d = mime_of_path(S("noext"));             /* "application/octet-stream" */
Str e = mime_ext_of(S("image/jpeg"));         /* "jpg"（反向查表取第一个命中） */

mime_is_text(t);        /* 1：text/*、JSON/XML/YAML/TOML 与 +xml/+json 后缀 */
mime_is_image(t);       /* 1 */
mime_charset(t);        /* "utf-8"；二进制类给空串 */

Str s = mime_sniff(bytes);                    /* 按魔数给类型；认不出给空 */
```

查不到就给 `application/octet-stream`（HTTP 的 Content-Type 需要一个能用的值），
不是空串。反向查表是**多对一**的（`jpg`/`jpeg` 都是 `image/jpeg`），所以
`mime_ext_of` 给的是表里第一个命中的扩展名，别当「唯一规范扩展名」用。

`mime_sniff` 只看**魔数**（PNG/JPEG/GIF/PDF/ZIP/gzip/xz/7z/bzip2/RIFF 的
WEBP-WAVE-AVI/OGG/FLAC/ID3/MP4/wasm/ELF/BMP），不做全文启发式：认不出的
（包括纯文本）返回空，要用扩展名兜底就自己接 `mime_of_path`。

### `inet`：地址、CIDR 与端口

```c
import std.inet;

Ip a = { 0 };
char buf[64] = { 0 };

inet_parse(S("192.168.0.1"), &a);          /* 1；a.family = IP_V4 */
inet_parse(S("2001:db8::1"), &a);          /* 1；a.family = IP_V6 */
inet_format(&a, buf, sizeof buf);          /* "2001:db8::1" */

Ip net = { 0 };
int prefix = 0;
Ip host = { 0 };

inet_parse_cidr(S("10.0.0.0/8"), &net, &prefix);      /* 1，prefix = 8 */
inet_parse(S("10.1.2.3"), &host);
inet_cidr_contains(&net, prefix, &host);              /* 1 */

u16 port = 0;
inet_parse_port(S("8443"), &port);                    /* 1 */
```

`Ip` 是纯值（`int family` + 16 字节），v4 用前 4 字节，所以同一个类型能装两种
地址、也能互相比较（`inet_eq` 要求族相同）。

解析的严格性是**刻意的**，为的是让「看起来像地址但其实不是」的东西尽早露出来：

* **IPv4** 四段、每段 0..255、**拒绝前导零**（`010.0.0.1` 有八进制歧义，
  `inet_pton` 也拒）、不跳过空白（要就自己先 `trim`）；
* **IPv6** 每组 1..4 个十六进制位、`::` 至多一次、末尾可内嵌点分 IPv4（占两组）；
  输出按 **RFC 5952**：最长零段压成 `::`（同长取第一个、单组零不压）、小写、
  无前导零，IPv4-mapped 一律写 `::ffff:a.b.c.d`；
* **端口**是纯十进制 0..65535（`65536`/`-1`/` 80`/`080` 都拒）。

分类函数按族各看各的号段，**v4-mapped 的 v6 先折回 v4** 再判——`::ffff:127.0.0.1`
算回环、`::ffff:10.0.0.1` 算私网，这是调用方最可能期待的行为。

格式化一律「先量长度再落笔」：放不下返回 -1，`dst` 一个字节都不动。

### `bitbuf`：位级读写

```c
import std.bitbuf;

char buf[16] = { 0 };
BitWriter w = bw_open(buf, (int)sizeof buf);

bw_put(&w, 2, 3)   catch e { return e.id; };   /* 版本 3 位 */
bw_put(&w, 0x1f, 5) catch e { return e.id; };  /* 标志 5 位 */
bw_put(&w, 300, 16) catch e { return e.id; };  /* 长度 16 位 */
bw_put_bytes(&w, S("hi")) catch e { return e.id; };
bw_bits(&w);                                   /* 40 */

BitReader r = br_open((char[:]){ buf, bw_bytes(&w) });
u32 ver = 0;
br_get(&r, 3, &ver) catch e { return e.id; };  /* ver = 2 */
```

* **位序是 MSB-first**（同一字节里先出现的是高位）——Deflate/JPEG 之外的格式
  基本都这样；要 LSB-first 请在调用方自己翻位；
* 写侧零分配，缓冲是调用方的：`bw_open` 不碰它，第一次写某字节时才把那一格
  清 0，所以没写到的地方保持原样；
* **越界是错误而不是截断**：写侧 `FULL`、读侧 `SHORT`，两种情况都**不动游标**、
  也不写半截（一次调用要么整段成功要么整段失败）；
* `bw_patch(w, at, v, n)` 在字节对齐处回填 8/16/32 位**且不移动游标**，
  用于「长度先留空、写完再补」；`bw_align`/`br_align` 补/跳 0 到字节边界；
* `bw_put_bytes`/`br_get_bytes` 要求已经在字节边界上，否则 `INVAL`（半个字节的
  读写在协议里没有意义）；
* `n == 0` 是合法空操作，`n` 超过 32/64 是 `INVAL`。

错误码记在 `w.err`/`r.err` 里（第一个错误的 id），`bw_ok`/`r.err` 可以事后查。

### `pqueue`：二叉堆

```c
import std.pqueue;
import std.sort;                       /* cmp_int */

@PQueue@int q = new PQueue@int;
q->init(&cmp_int, 0);                  /* 0 = 小顶堆，1 = 大顶堆 */

q->push(5) catch e { return e.id; };
q->push(1) catch e { return e.id; };

int top = 0;
q->peek(&top);                         /* 1，top = 1 */
q->pop(&top);                          /* 1，top = 1 */
```

和 `std.vec` 同族：托管容器、只能 `new`、方法接收者是指针。比较器是 **qsort
风格** `int (*cmp)(const void *, const void *)`，和 `std.sort` 的 `cmp_int`/
`cmp_u32`/`cmp_str` 直接通用（`vec->sort` 也是这套）。**没设比较器时按字节序**
（`memcmp`）比，和 `sort_lex` 同口径：纯无符号小整数够用，负数会排到正数后面。

| 方法 | 说明 |
| --- | --- |
| `push(v)` | 压入，摊销 O(log n)；分配失败给 `std.err.NOMEM` |
| `pop(&out)` / `peek(&out)` | 弹顶/看顶；堆空返回 0（不是错误） |
| `push_pop(v, &out)` | 弹顶再压入，一次 sift，top-k 的标准写法 |
| `heapify()` | 用现有 `data[0..n)` 重建堆（整段读入之后） |
| `is_valid()` | 堆序检查，测试/断言用 |

top-k 的惯用法（留最大的 3 个）：小顶堆 + 满了就 `push_pop`——堆顶是当前
「入选里最小的」，新值比它大才替换。

### `ini`：INI 配置

```c
import std.ini;

char *text = "[db]\nhost = localhost\nport = 5432\n";
char[:] t = { text, (int)strlen(text) };

IniReader r = ini_open(t);
IniEntry e = { { { (char *)0, 0 } }, { { (char *)0, 0 } }, \
               { { (char *)0, 0 } }, 0 };
while (ini_next(&r, &e) == 1) {
    /* e.section / e.key / e.value 都是输入上的视图，e.line 是行号 */
}

Str host = { { (char *)0, 0 } };
ini_lookup(t, S("db"), S("host"), &host);      /* 1 */
```

和 `std.csv` 一个思路：**不分配、不复制**，字段都是指向输入文本的视图，所以输入
必须活到用完为止。语法有意做小：

* 行分隔 `\n`（`\r\n` 也认）；空行忽略；trim 之后以 `#` 或 `;` 开头的行是
  **整行注释**——行内的 `#` 不是注释（值里可以有 URL 片段）；
* `[section]` 切节，节名 trim，同名节等于合并；第一节之前的键属于**根区**
  （`section` 是空视图）；
* `key = value` 用**第一个** `=` 分隔，两侧 trim，所以值里可以再出现 `=`；
  值可以为空（`k =`），键不能为空；
* 其它非空行都是**语法错误**：`ini_next` 返回 -1，行号在 `r.err`（1 起），
  之后可以继续读下一行；`ini_lookup`/`ini_has_section`/`ini_count` 遇到坏行
  直接放弃（`ini_count` 给 -1），不假装读到了后面的键；
* 键或节重复时 `ini_lookup` 给**最后一个**（「后写覆盖先写」）。

不做类型转换、不处理转义、不合并续行、不解析引号——拿到 `Str` 之后用
`std.str` 的 `to_int`/`trim` 等自己处理。

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

### 集成示例：`examples/stdtour.c6`

```sh
make std-tour
```

```
stdtour: 标准库新模块一日游
strbuf  -42 deadbeef             std/vec.c6 ...
path    docs + stdlib.md = stdlib [.md]
hash    fnv32=9cccb87e fnv64=b142c6921124ac1e crc32=123456789->cbf43926
bits    0x0101010101010101 parity=0
utf8    14 bytes / 9 runes, first=19990
deque   窗口=[3 4 5 6] 加上 1 2 后 sum=21
stdtour: 73 check(s), 0 failure(s)
```

六个新模块各一节，每节都带自检（失败打 `FAIL`，最后按失败数决定退出码），
所以它既是演示也是集成用例：strbuf 拼串、path 拆装、hash 已知向量、bits
位运算、utf8 的合法/非法输入、deque 滚动窗口。

### 集成示例：`examples/stdkit.c6`

```sh
make std-kit
```

```
stdkit: 标准库第三批一日游
stdkit: 70 check(s), 0 failure(s)
```

第三批的六个模块各一节：ascii 分类与忽略大小写比较、num 的 `%f`/`%e`/`%g`
和解析往返、codec 的 Base64 已知向量与非法输入、rand 的可重放/区间/shuffle、
fs 的 mkdir_p → 写文件 → stat → 目录遍历 → rename → 清理、set 的增删改查与
迭代器。示例里 `set` 依赖 `std/map.c6` 的正文，所以 `STDKIT_SRCS` 把它列在
`std/set.c6` 前面。

### 集成示例：`examples/stdbox.c6`

```sh
make std-box
```

```
stdbox: 标准库第四批一日游
stdbox: 59 check(s), 0 failure(s)
```

第四批的四个模块各一节：env 的 set/get/unset、数值与布尔解析、`argc/argv`；
csv 的引号字段、跨行字段、CRLF、列求和、写引号字段与整行；json 的嵌套取值、
数组下标、`2^53+1` 的原文保真、紧凑/缩进写出、转义往返、语法错报 `PARSE`；
sha256 的空串与 `"abc"` 向量、分片与一次性一致、RFC 4231 的 HMAC、对 JSON 文本
求摘要。`STDBOX_SRCS` 里 `std/json.c6` 要带上 `std/mem.c6`、`std/str.c6`、
`std/num.c6` 的正文，`std/sha256.c6` 要带上 `std/bits.c6`（`rotr32` 是跨 TU
调用）。

## 写法约定（照着写就不会踩坑）

* **普通结构体的方法就是「第一个参数是结构体指针」的文件作用域函数**：
  `int count(Str *self)`；泛型结构体才能在 `struct` 体内写方法体
  （`struct Split { int has() { ... } }` 会被拒：`a function cannot be defined
  inside a struct`）。
* **普通结构体的方法名要带「接收者前缀」**（`reader_read_line`、
  `writer_flush`、`strbuf_write`、`arena_alloc`、`random_below`、
  `hasher_update`、`dir_next`、`duration_format`）。原因：体内写不了方法体，
  写在体外的函数除了 `T_方法名` 还会**按裸名发一个弱别名**给纯 C 侧链接，
  于是 `write`、`push`、`clear`、`update`、`close`、`alloc` 这些通用名字会同时
  出现在整个程序里，谁手写一个同名函数就互相顶掉（弱别名只指向本 TU 第一个取到
  该名字的定义）。带前缀后调用侧读起来仍然是方法（`pr->read_line(...)`
  就是 `reader_read_line(&pr, ...)`），但全局符号名不会撞。
* **例外：`foreach` 的迭代器协议只认字面上的 `has()` / `next()`**
  （`tcc/tccgen.c` 的 `c6_iter_proto_of` 用 `tok_alloc_const("has"/"next")`
  找方法），所以 `Split`/`Lines`/`Fields`/`Range`/`StepRange`/`CFile` 之外的
  迭代器不能把这两个方法改名；反过来，这类方法一律**不加前缀**，否则
  `foreach` 会报「no 'has'/'next' iterator」。同名的 `has`/`next` 分属不同
  tag 时彼此的弱别名会互相覆盖，但方法调用（`it->has()`）不受影响。
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
* **切片的字段按设计是只读的**：切片是 `struct { T *const data; const size_t
  len; }`，`char[:] p; p.len -= 1;` 会得到一条 `assignment of read-only
  location` 警告——而且**照样会改**（tcc 对 `const` 只警告），所以别依赖它。
  要改视图就按视图重建：`(char[:]){ p.data, n }`（`std/path.c6` 的
  `strip_trailing`）。
* `catch` 体里不能只写普通语句（必须 `return`/`err()`/`goto` 收尾），所以
  「失败了记一笔再继续跑」这种写法要包一层：`static int put_int(StrBuf *b,
  int v) { b->write_int(v) catch e { return -1; }; return 0; }`，调用方
  `check(put_int(b, -42) == 0, "...")`（见 `examples/stdtour.c6`）。
* 在 `T!error` 函数里转发错误用 `try f(...)`；要拿它的**返回值**参与判断就写
  `(try f(...))`：`if ((try s->add(7)) != 0)`。`catch` 只交出错误，拿不到成功值。
* 在**导入方**的文件作用域写 `static int f(Set@int *s) { return s->has(7); }`
  会得到 `assignment makes integer from pointer` 警告——跨 TU 用实例类型时方法
  的返回类型没被正确解析（能跑，但别依赖）。把这段逻辑放回定义模块，或在导入方
  用局部 `@Set@int` 变量直接调方法（`examples/stdkit.c6` 的 `tour_set` 这么绕）。
* `#include` 要写在 `module` 前面；`main` 不能出现在模块里。

## 已知限制（编译器层面，标准库已绕开）

用标准库时可能碰到的、来自编译器而不是库本身的限制：

| 限制 | 现状 / 绕法 |
| --- | --- |
| 模板体内方法不能返回**多参数泛型实例**或**元组**（`Entry@(K,V) next()`、`(K,V) next()` → `declaration expected`） | 返回单参数类型或标量，多出来的东西走 `cur` 字段（`Map` 的迭代器就是这么做的），或者干脆不提供工厂方法 |
| 泛型**值形参**的模板写 `struct Buf@(int N)`、实例写 `Buf@(8)`（`Buf@(int, 8)` 会被当成两个实参） | 记住「声明带类型和名字，实例只给值」 |
| **普通**（非泛型）结构体不能在 `struct` 体内写方法体（`a function cannot be defined inside a struct`） | 方法写成「第一个参数是结构体指针」的文件作用域函数，调用写 `(&v)->m()` |
| 文件作用域里写「参数/返回类型带实例类型」的辅助函数，会让**导入这个模块的每个 TU** 都提前实例化该类型 | 别这么写；需要时把逻辑放进 `test` 块或用标量参数（`std/fmt.c6` 的测试用 `goto` 取错误码）。在导入方写 `static int f(Set@int *s)` 再调 `s->has()` 还会得到 pointer/integer 警告（返回值类型没解析对），见上面「写法约定」 |
| ~~往 `Buf@(N)` 里加若干形状相同的方法会挂~~（曾经的 `skip(')')` → `end_macro()` 死循环） | **已消失**：同形状方法（`char tmp[16]` + 进/移位 + `(char)` 强转）加到五个也编得过、跑得通，`std/fmt` 的 `write_u64`/`write_hex`/`write_bin`/`write_bool`/`write_ptr` 就是照这个形状补的（`tccpp.c` 的宏重放改写之后不再复现） |
| ~~把 `Buf@(N)` 的 `write_hex` 形参从 `u32` 加宽到 `u64` 会**挂住编译器**~~（当时 `-c` 无输出直到超时，退出码 124） | **现在复现不出来**：当前 `std/fmt.c6` 的 body、旧版 body、带 `(char)` 强转的写法都在 1 秒内编过，`-test` 也是 10 个块全过。`write_hex(u32)`/`write_hex64(u64)` 双入口保留只是为了不动 API |

这些限制在 `docs/reference/grammar.md`（约束表 C1xx）和 `docs/design/plan.md` 里有对应的
条目；标准库的写法就是它们当前的「推荐用法」。

## 相关回归用例

* `tests/generic_scope.c6` + `tests/tu/generic_scope.c6`：模板实例的作用域
  边界（字段名 vs 同名方法、方法形参名不漏到调用方、两个 TU 都实例化同一
  模板时方法体是弱符号）。
* `make stdtest`：`std/` 下每个模块的全部 `test` 块。
* `make std-demo`：`examples/wordcount.c6` 那个集成示例。
* `make std-tour`：`examples/stdtour.c6`，新模块的拼装示例（自带自检）。
* `make std-kit`：`examples/stdkit.c6`，第三批模块的拼装示例（自带自检）。
* `make std-box`：`examples/stdbox.c6`，第四批模块的拼装示例（自带自检）。
* `make std-web`：`examples/stdweb.c6`，第五批模块的拼装示例（自带自检）。
* `make std-net`：`examples/stdnet.c6`，第六批模块的拼装示例（自带自检）。
* `std/inet.c6` 的「RFC 5952 压缩」用例：8 组输入对 8 组期望输出，含
  `2001:db8:0:0:1:0:0:1`、同长零段取第一个（`1:0:0:2:0:0:3:4`）、
  单组零不压（`2001:db8:0:1:1:1:1:1`）与只有单零段的 `0:0:1::`。
* `std/bitbuf.c6` 的「整段字节」用例：写/读两侧共用一套边界，钉住
  `bw_put_bytes`/`br_get_bytes` 的起始下标（曾经在循环里用增长中的游标当下标，
  第二字节写到了下一格）。
* `std/pqueue.c6` 的「交错 push/pop」：100 次混合操作，每步都跑 `is_valid()`，
  钉住 sift_up/sift_down 的四种交换方向。
* `std/url.c6` 的「RFC 3986 §5.4 的相对解析」：23 条 normal + abnormal 用例
  （`../..//g`、`g;x=1/../y`、结尾 `/.` 与 `/..` 都在里面），钉住 `remove_dots`
  的每个分支；另有「全零初始化的 `Url` 不该给出端口」的回归。
* `std/uuid.c6` 的 v5 已知向量：与 Python `uuid.uuid5` 逐字节一致
  （`www.example.com` / `python.org` / `www.widgets.com`）。
* `std/map.c6` 的「墓碑不会在扩容时复活」：删除留下墓碑后继续插入触发扩容，
  验证墓碑不会被搬进新表（写 `std/set` 时踩到的真实 bug）。
