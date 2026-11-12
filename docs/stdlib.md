# c6 标准库 `std/`

> **状态**：现行 API · **索引**：[文档索引](README.md) · **用法**：[manual/](manual/) · **诊断/号段**：[reference/grammar.md](reference/grammar.md)（§8.1）
>
> 一句话：`std/` 的设计原则、每个模块的 API、写法约定，以及编译器层面已知限制。

标准库放在仓库根的 `std/` 下，**一个文件一个模块**，模块名就是它的路径：
`std/container/vec.c6` 声明 `module std.container.vec;`，消费侧写 `import std.container.vec;`（编译时把仓库
根放进 `-I`，或就在仓库根下编译）。

文件按领域分七个子目录，**目录是模块名的一部分**（改名就是移动文件，反之亦然）：

| 目录 | 领域 |
| --- | --- |
| `std/core/` | 基础：定宽类型、错误码、内存、数学、位运算、哈希、随机 |
| `std/text/` | 文本：字符串视图与缓冲、格式化、UTF-8、ASCII、浮点、表格与自然排序 |
| `std/container/` | 容器与算法：向量、跨度、映射、集合、双端队列、优先队列、区间、排序、图、LRU、前缀树 |
| `std/os/` | 操作系统：流、文件系统、路径、环境变量、时间、命令行参数、子进程 |
| `std/encoding/` | 编解码与压缩：Base64/Base85/十六进制/百分号、SHA-1/SHA-256、位流、UUID、LZ4/RLE、hexdump |
| `std/data/` | 数据格式：JSON、CSV、INI、TOML、XML、查询串/表单 |
| `std/net/` | 网络与 Web：TCP 套接字、DNS 报文、WebSocket 帧、IP/CIDR、URL、MIME |

```sh
./tcc/c6 -B tcc -I . -I examples -o build/wordcount \
    examples/wordcount.c6 examples/wordcount/text.c6 \
    std/core/types.c6 std/core/err.c6 std/text/str.c6 std/container/sort.c6 \
    std/container/vec.c6 std/container/span.c6 std/text/fmt.c6 std/container/range.c6
```

> `import` 只带来**声明**：模块的函数体要把它自己的 `.c6` 文件一起编进程序。
> 上例里用到 `std/` 下哪个 `.c6` 就编哪个；`make std-demo` 是这条命令的缩写。
>
> 模块之间有依赖：`std/container/vec.c6` 的 `sort`/`sort_lex` 用 `std/container/sort.c6` 的内核，
> 而 `std/container/sort.c6` 又 `import std.text.str`/`std.core.err`/`std.core.types`——所以用
> `std.container.vec` 时这几份正文都得在命令行上，漏了会报
> `unresolved reference to 'std__container__sort__sort_bytes'`。

**`std/core/`**（基础：定宽类型、错误码、内存、数学、位运算、哈希、随机）

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `std/core/atomic.c6` | `std.core.atomic` | 原子操作与自旋锁（零分配、不依赖 pthread）：`Atomic`（i64）/`Atomic32`（int）的 `load`/`load_relaxed`、`store`/`store_release`、`add`/`sub`（新值）、`fetch_add`/`fetch_sub`（旧值）、`exchange`、`cas`（相等才换，失败回写当前值）、`and_with`/`or_with`/`xor_with`、`inc`/`dec`；屏障 `fence`/`fence_acquire`/`fence_release`；TAS 自旋锁 `SpinLock` 的 `spin_init`/`lock`/`try_lock`/`unlock`/`is_locked` |
| `std/core/bignum.c6` | `std.core.bignum` | 任意精度整数（1024 位、零分配）：`big_parse`/`big_parse_base`（2..36 进制）/`big_from_i64`/`big_from_u64`、`Big` 的 `add`/`sub`/`mul`/`div`/`mod`/`divmod`（截断除法）/`pow`（u32 指数）、`cmp`/`is_zero`/`is_neg`、`to_i64`/`to_u64`、`format`/`format_base`/`digits`；容量超限、除零、解析失败都给 `err` |
| `std/core/complex.c6` | `std.core.complex` | 复数 `Complex`（f64 实部/虚部）：`cplx`/`cplx_real`/`cplx_i`、`add`/`sub`/`mul`/`div`/`neg`/`conj`/`inv`/`scale`/`normalize`、`abs2`/`abs`（用 `std.core.math` 的 `sqrt`）、`pow_int`（快速幂，负指数取倒数）、`eq`/`near`/`is_finite`/`is_zero`；除零按 IEEE 给 inf/nan |
| `std/core/bits.c6` | `std.core.bits` | 位运算：`popcount32/64`、`parity32`、`clz32/64`、`ctz32/64`（0 输入给 32/64）、`rotl`/`rotr`、`bswap16/32/64`、`bit_get/set/clear/toggle`、`mask_low`/`mask_range`、`sign_extend`、`is_pow2_32/64` |
| `std/core/decimal.c6` | `std.core.decimal` | 十进制定点 `Dec`（i64、单位 1e-9，范围约 ±9.22e9）：`dec_parse`/`dec_from_i64`/`dec_from_f64`/`dec_from_units`、`add`/`sub`/`mul`/`mul_int`/`div`（乘除走 128 位中间值再按 half-up 收尾，不经过 double）、`round`（`DEC_TRUNC`/`DEC_HALF_UP`/`DEC_HALF_EVEN`/`DEC_FLOOR`/`DEC_CEIL`）、`to_i64`/`to_f64`、`format`（恰好 N 位小数）；溢出/除零/解析失败给 `err` |
| `std/core/crc.c6` | `std.core.crc` | 嵌入式校验（逐位、不带表）：CRC-8 的 `crc8_smbus`/`crc8_maxim`、CRC-16 的 `crc16_ccitt`/`crc16_xmodem`/`crc16_modbus`/`crc16_ibm`、`crc32c`；流式原语 `crc8_update`/`crc16_update`/`crc32_update`（poly 正规形式 + `refin`）；`fletcher16`/`lrc8`/`xor8`/`sum8`/`sum16` |
| `std/core/err.c6` | `std.core.err` | 错误码 `enum Error`（`OK`…`BUSY`，16 个）与 `msg()`/`msg_of()`/`is_ok()`；号段固定在 `0x6000` |
| `std/core/fixed.c6` | `std.core.fixed` | 定点：Q16.16 的 `Fix`（`fix_from_raw`/`fix_from_int`/`fix_from_frac`/`fix_from_f64`、`to_f64`/`to_int`/`round`/`floor`/`ceil`/`frac`/`abs`/`neg`、`add`/`sub`/`mul`/`div`/`sqrt`/`lerp`/`clamp`/`cmp`/`sin`/`cos`，溢出饱和）与 Q15 的 `Q15`（`q15_from_int`/`q15_from_f64`/`to_f64`/`mul`/`add_sat`/`sub_sat`） |
| `std/core/hash.c6` | `std.core.hash` | 哈希：FNV-1a 32/64（`fnv1a_bytes`/`fnv1a_cstr`/`fnv1a64_bytes`）、`crc32_bytes`、`adler32_bytes`，以及流式 `Hasher`（`hasher`/`update`/`digest`） |
| `std/core/math.c6` | `std.core.math` | 整数：`min`/`max`/`clamp`/`iabs`/`gcd`/`lcm`/`ipow`/`ipow_u64`/`imul_checked`/`sat_add`/`sat_mul`/`is_pow2`/`next_pow2`/`floor_div`/`mod`；浮点（不链 libm）：`sqrt`/`floor`/`ceil`/`trunc`/`round`/`fabs`/`nearly_eq`/`clamp_f64`/`fact` |
| `std/core/mem.c6` | `std.core.mem` | 借用式内存池 `Arena`（`alloc`/`alloc_array`/`reset`/`room`/`used`）、托管堆薄包装 `alloc_new`/`alloc_new_array`/`dealloc`、按元素的 `copy`/`fill`/`zero`/`swap`/`rotate`/`reverse_bytes`、`align_up`/`align_down`/`is_aligned` |
| `std/core/pool.c6` | `std.core.pool` | 定长块内存池 `Pool`（零分配、O(1)）：`pool_make`/`init`、`alloc`/`free`（拒绝池外/不对齐指针）、`contains`/`count`/`capacity`/`used`/`block_size`/`is_empty`/`is_full`/`reset`；块与基址按 8/16 字节对齐 |
| `std/core/rand.c6` | `std.core.rand` | xorshift64* 伪随机：`rand_seed` + `Rand` 方法 `next_u64`/`next_u32`/`below`（拒绝采样）/`int_range`/`int_range_incl`/`next_f64`/`next_bool`/`fill`/`shuffle`/`shuffle_int` |
| `std/core/rational.c6` | `std.core.rational` | 精确有理数 `Rat`（i64 分子/分母，始终最简、`den > 0`）：`rat_make`/`rat_from_i64`/`rat_parse`（`a/b` 或 `a`）、`add`/`sub`/`mul`/`div`/`inv`/`neg`/`abs`、`cmp`（连分数比较，交叉相乘会溢出也不经过 double）、`to_i64`/`to_f64`、`format`（`n/d`）；加减的中间分子溢出报 `err` |
| `std/core/types.c6` | `std.core.types` | 定宽类型 `u8…u64`/`i8…i64`、`usize`/`isize`、`f32`/`f64`、`byte`、`bool`，以及 `Limits`（`U8_MAX`…`I32_MIN`）/`Bool` 两个枚举 |

**`std/text/`**（文本：字符串视图与缓冲、格式化、UTF-8、ASCII、浮点转换）

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `std/text/ascii.c6` | `std.text.ascii` | ASCII 判别与转换（零分配、零依赖）：`ascii_is_digit`/`is_alpha`/`is_alnum`/`is_space`/`is_print`/`is_punct`/`is_cntrl`、`ascii_digit_value`/`hex_value`/`hex_char`、`ascii_to_lower`/`to_upper`、原地 `ascii_lower`/`upper`、`ascii_cmp_nocase`/`eq_nocase` |
| `std/text/fmt.c6` | `std.text.fmt` | 定长格式化缓冲 `Buf@(N)`：`write_int`/`write_uint`/`write_u64`、`write_hex`/`write_hex64`/`write_bin`、`write_bool`/`write_ptr`、`write_pad`、拼接；数字转换原语 `digits_u32`/`digits_u64`/`digits_hex` 与 `std.text.strbuf` 共用 |
| `std/text/num.c6` | `std.text.num` | 浮点格式化与解析（不链 libm）：`fmt_f64`（固定位）、`fmt_f64_sci`（`%e`）、`fmt_f64_g`（`%g` 语义、去尾零）、`round_to`、`f64_is_nan`/`f64_is_inf`、`parse_f64`/`parse_f64_cstr`（inf/nan、`INVAL`/`PARSE`/`RANGE`） |
| `std/text/str.c6` | `std.text.str` | 字符串视图 `Str`（`char[:]` 的包装，不分配）与字面量宏 `S()`：切分 `split`/`lines`/`join`、查找 `find`/`last_find`/`contains`/`count_char`/`count_sub`、裁剪 `trim`/`trim_left`/`trim_right`、转换 `to_int`/`to_base`/`copy_upper`/`copy_lower`、`cmp`/`cmp_str`/`cmp_cstr`、`replace`/`split_once`、`hash` |
| `std/text/template.c6` | `std.text.template` | `{{name}}` 文本模板（零分配）：`tpl` 工厂、`Template` 的 `render`/`render_len`/`render_opt`/`vars`；名字两侧空白忽略、`{{{{` 输出字面量 `{{`、单独 `}}` 原样、缺变量严格 -1（`render_opt(..., 0)` 给空串）、值不递归展开、`vars` 去重收集名字、放不下不写半截 |
| `std/text/strbuf.c6` | `std.text.strbuf` | 会自己长大的字符缓冲 `StrBuf`（托管引用）：`write`/`write_cstr`/`push`、`write_int`/`write_u64`/`write_hex`、`reserve`/`truncate`/`clear`/`slice`/`cstr`（依赖 `std.text.fmt` 的数字原语） |
| `std/text/utf8.c6` | `std.text.utf8` | UTF-8 解码 `utf8_decode`（返回 1..4，拒绝过长编码/代理区/超范围/截断）、编码 `utf8_encode`、`utf8_valid`/`utf8_count`、`Rune` 与 `utf8_runes()` 迭代器 |
| `std/text/glob.c6` | `std.text.glob` | shell 风格 glob（零分配）：`Glob` 的 `match`/`match_path`/`has_magic`、`glob` 工厂、`*`/`?`/`[a-z]`/`[!…]`/`\` 转义；`match_path` 的 `*`/`?` 不跨 `/` |
| `std/text/semver.c6` | `std.text.semver` | 语义化版本 2.0.0：`semver_parse` 工厂、`Semver` 的 `compare`/`is_prerelease`/`format`；严格前导零/标识校验，构建标识不参与优先级 |
| `std/text/utf16.c6` | `std.text.utf16` | UTF-16 解码/编码/计数：`Utf16` 的 `decode`/`valid`/`count`、`utf16_encode`/`utf16_encoded_len`；代理对、BMP、非法代理与截断（零分配） |
| `std/text/ansi.c6` | `std.text.ansi` | ANSI 转义构造器 `Ansi`：`reset`/`bold`/`fg_basic`/`bg_basic`/`fg`/`bg`/`move`/`up`/`down`/`clear_screen`/`clear_line`/`hide_cursor`/`show_cursor`，满了置 `ok=0` |
| `std/text/natural.c6` | `std.text.natural` | 自然排序与人类可读化：`natural_cmp`/`natural_cmp_ci`（连续数字段按整数比较，前导 0 视为相等，超长数字段不溢出）、`humanize_bytes`/`parse_bytes`（1024 进制 `B/KiB/MiB/GiB/TiB`，一位小数、四舍五入）、`humanize_duration`/`parse_duration`（`1h 2m 3s`/`850ms`/裸数字按秒）、`humanize_int`（千分位，含 I64_MIN）；都写调用方缓冲、放不下不写半截 |
| `std/text/table.c6` | `std.text.table` | 按显示列宽对齐的表格：`table_make`、`Table` 的 `border`/`sep`/`pad` 与 `render`/`table_len`、`TBL_LEFT`/`TBL_RIGHT`/`TBL_CENTER`；列宽用 `std.text.width`（CJK 2 列），无表头/无边框/无竖线/紧贴都支持，先量再写 |
| `std/text/width.c6` | `std.text.width` | 终端显示宽度（Unicode 15.0）：`cp_width`、`text_width`/`text_width_checked`、按列裁剪补齐 `text_prefix_len`/`text_fit`/`text_pad`/`text_clip`；W/F 算 2 列、组合符/格式符 0 列、只在码点边界切 |
| `std/text/regex.c6` | `std.text.regex` | 回溯式正则（零分配、按码点）：`re_compile`/`re_compile_flags`（`RE_ICASE`/`RE_DOTALL`/`RE_MULTILINE`）、`Regex` 的 `find`/`find_from`/`match_at`/`matches`/`full`/`find_all`/`groups`/`is_valid`/`error_pos`、`Match` 的 `span`/`group`；`*`/`+`/`?`/`{m,n}`（懒惰后缀）、捕获与非捕获组、分支、字符类、`\d\w\s\b`、锚点；位置是字节、匹配按码点，容量上限见枚举 |
| `std/text/diff.c6` | `std.text.diff` | 行级差异：`diff_make`（Hirschberg LCS，时间 O(na*nb)、空间 O(nb)，工作区从 `Arena` 分）、`Diff` 的 `write_script`/`write_unified`/`script_len`/`unified_len`/`op_at` 与 `added`/`removed`/`same`；先剥公共前后缀，hunk 按 ctx 合并，放不下不写半截 |
| `std/text/fuzzy.c6` | `std.text.fuzzy` | 模糊匹配（按码点、零分配）：`lev_distance`/`lev_distance_max`/`lev_ratio`（千分比）、`damerau_distance`（OSA，相邻换位算 1）、`subsequence_match`/`subsequence_score`（连续与词首加分）、`fuzzy_best`（候选里挑最近的）；`FUZZY_MAX`=256 码点，超了给 -1 |
| `std/text/wrap.c6` | `std.text.wrap` | 按显示列折行与排版：`wrap_make`/`wrap_indented`、`Wrap` 的 `count`/`write`/`lines`、`text_indent`/`text_indent_len`/`text_center`/`text_center_len`；CJK 2 列、组合符 0 列、CJK 之间可直接断行、超长词按码点硬切（`WRAP_BREAK_LONG`）；先量再写，放不下不写半截 |

**`std/container/`**（容器与算法：向量、跨度、映射、集合、双端队列、优先队列、区间、排序）

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `std/container/deque.c6` | `std.container.deque` | 环形缓冲双端队列 `Deque@T`（托管引用）与 `DIter@T`：`push_back`/`push_front`/`pop_back`/`pop_front`、`first`/`last`/`at`/`get`/`set`/`reserve`/`clear` |
| `std/container/chan.c6` | `std.container.chan` | 有界通道（CSP 风格，缓冲由调用方给）：`chan_init`（`buf`/`cap`/`esize`）、`send`/`recv`（满/空阻塞）、`try_send`/`try_recv`（不阻塞）、`send_ms`/`recv_ms`（限时，1/0/-1）、`close_chan`（幂等，关后 sender 立刻失败、receiver 取完剩余再失败）、`count`/`capacity`/`is_empty`/`is_full`/`is_closed`/`sent_count`/`recv_count`；元素按字节块拷贝（`esize`），int/指针/结构体都能过；正文依赖 `std.os.sync` |
| `std/container/map.c6` | `std.container.map` | 开放寻址哈希表 `Map@(K, V)`、`Entry@(K, V)`、`Iter@(K, V)`：`put`/`get`/`get_or`/`set`/`put_new`/`remove`/`clear` |
| `std/container/pqueue.c6` | `std.container.pqueue` | 二叉堆 `PQueue@T`（托管容器）：`push`/`pop`/`peek`/`push_pop`/`heapify`/`clear`、`count`/`capacity`/`is_empty`/`is_valid`、`init`/`set_cmp`/`set_max`（比较器与 `std.container.sort` 的 `cmp_int`/`cmp_str` 通用） |
| `std/container/range.c6` | `std.container.range` | 能被 `foreach` 吃掉的区间 `Range`/`StepRange` |
| `std/container/set.c6` | `std.container.set` | 哈希集合 `Set@K`（`std.container.map` 的薄包装）与 `SetIter@K`：`add`（1 新增 / 0 已存在）/`remove`/`has`/`count`/`capacity`/`reserve`/`clear`/`iter` |
| `std/container/sort.c6` | `std.container.sort` | 比较器驱动的排序与查找：`sort_bytes`/`sort_int`/`sort_u32`/`sort_f64`（稳定归并 + 小数组插入）、`find_bytes`/`find_int`/`find_u32`、`lower_bound_int`/`upper_bound_int`、`cmp_int`/`cmp_u32`/`cmp_f64`/`cmp_int_desc`、`is_sorted_int` |
| `std/container/bitset.c6` | `std.container.bitset` | 借用调用方 `u64` 缓冲的位集合 `Bitset`：`set`/`clear`/`toggle`/`bit`/`count`/`first_set`/`next_set`、`set_all`/`clear_all`、`union_with`/`intersect_with`/`difference_with`/`toggle_with`/`equals`/`copy_from` |
| `std/container/list.c6` | `std.container.list` | 侵入式双向链表 `List`/`ListNode`：`init`/`push_back`/`push_front`/`pop_front`/`pop_back`/`remove`/`clear`/`contains`、`ListIter` 的 `has`/`next` |
| `std/container/ring.c6` | `std.container.ring` | 借用调用方数组的固定容量环形队列 `Ring`：`ring_make`、`push`（满不覆盖）/`pop`/`peek`/`at`/`clear`、`count`/`capacity`/`is_empty`/`is_full` |
| `std/container/span.c6` | `std.container.span` | 别人内存上的窗口 `Span@T`（纯值，零拷贝） |
| `std/container/graph.c6` | `std.container.graph` | 邻接表图（前向星，借用调用方数组）：`graph_init`/`add_edge`、`Graph` 的 `bfs`（无权最短路）/`dfs`（迭代前序，显式栈）/`toposort`（Kahn，有环返回 0）/`components`（并查集弱连通分量）/`dijkstra`（非负权 + `prev` 回溯链）；队列/栈/工作区都由调用方给 |
| `std/container/lru.c6` | `std.container.lru` | LRU 缓存（链式哈希表 + 双向链表，O(1)）：`lru_init`、`Lru` 的 `get`（命中挪到头部）/`peek`（不动顺序）/`put`（满了淘汰尾部）/`remove`/`clear`/`size`/`capacity`/`is_empty`；`hits`/`misses`/`evicts` 统计；数组全由调用方给 |
| `std/container/region.c6` | `std.container.region` | 半开区间 `[lo, hi)` 集合：`region_init`、`Region` 的 `add`（重叠与相邻合并、失败不改内容）/`contains`（二分）/`overlaps`（相邻不算）/`find_gap`（第一个够长的空隙，带溢出保护）/`total`/`count_region`/`clear`；i64 坐标、允许负数 |
| `std/container/trie.c6` | `std.container.trie` | 字节前缀树（节点池由调用方给）：`trie_init`、`Trie` 的 `put`（新键 1/覆盖 0/池满 -1）/`get`/`has_prefix`/`remove`（只清词尾标记）/`clear`/`size`/`node_count`；键是任意字节序列（含 `\0`，长度显式传） |
| `std/container/vec.c6` | `std.container.vec` | 会长的数组 `Vect@T`（托管引用，`new` 出来）与 `Iter@T`：`push`/`pop`、`insert`/`remove_at`/`truncate`、`first`/`last`、`index_of`/`contains`、`reverse`、`slice`/`extend`、`sort`/`sort_lex`、`each`/`filter`/`fold` |

**`std/os/`**（操作系统：流、文件系统、路径、环境变量、时间）

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `std/os/env.c6` | `std.os.env` | 环境变量与命令行（POSIX）：`env_get`/`env_has`/`env_set`/`env_unset`、`env_home`/`env_tmpdir`/`env_path`/`env_user`、`env_get_int`/`env_get_u64`/`env_get_bool`、`env_args`/`env_argc`/`env_arg`/`env_prog` |
| `std/os/fs.c6` | `std.os.fs` | POSIX 文件系统元数据与目录：`StatInfo`、`fs_stat`/`fs_lstat`/`fs_exists`/`fs_is_dir`/`fs_is_file`/`fs_size`、`fs_mkdir`/`fs_mkdir_p`/`fs_remove`/`fs_rmdir`/`fs_rename`、`fs_getcwd`/`fs_chdir`、零分配 `Dir` 遍历 |
| `std/os/io.c6` | `std.os.io` | 文件与标准流：`Reader`（`read_line`/`read_all`/`read_bytes`/`seek_to`/`tell`/`eof`/`close`）、`Writer`（`write_line`/`write_bytes`/`write_slice`/`flush`/`close`）、零分配的按行迭代 `CFile`（`has`/`next`）；调用一律写 `pr->read_line(...)` |
| `std/os/path.c6` | `std.os.path` | POSIX 路径：`path_base`/`path_dir`/`path_ext`/`path_stem`/`path_is_abs`/`path_has_ext`/`path_split`、`path_join`/`path_normalize`（写调用方缓冲，放不下返回 -1 且不写半截） |
| `std/os/args.c6` | `std.os.args` | 命令行选项解析（零分配、按规格表迭代）：`args_open` + `Args.next(specs, n, &arg)`、`ArgSpec`（长名/短名/`has_value`）、`Arg` 的 `kind`/`spec`/`name`/`value`/`index`、`args_find`、`Args.positional_count`；`--name=value`、`--name value`、`-n value`、`-n=value` 都认，`--` 之后全按位置参数，未知选项/开关带值/缺值分别报 `ARG_EUNKNOWN`/`ARG_EMISSING` |
| `std/os/proc.c6` | `std.os.proc` | 子进程（POSIX、零分配）：`proc_run`（fork+execvp+wait）、`proc_spawn`/`proc_wait`、`proc_run_sh`（`/bin/sh -c`，命令串 4KB 上限）、`proc_which`（带 `/` 直接查，否则按 `PATH`）；`ProcStatus` 拆出 `exited`/`exit_code`/`signaled`/`signal`，命令找不到按 shell 惯例退出码 127 |
| `std/os/sync.c6` | `std.os.sync` | 阻塞式同步原语（pthread 薄封装，值类型）：`Mutex`（`init`/`destroy`/`lock`/`try_lock`/`unlock`）、`Cond`（`wait`/`wait_ms`/`signal`/`broadcast`）、`RWLock`（`rdlock`/`try_rdlock`/`wrlock`/`try_wrlock`/`unlock`）、`Once`（`call(fn)` 只跑一次）、`Semaphore`（`init(v)`/`wait`/`try_wait`/`wait_ms`/`post`/`value`）、`Barrier`（`init(n)`/`wait`）；会失败返回 1/0，限时等待 1/0/-1 |
| `std/os/thread.c6` | `std.os.thread` | 线程身份与线程池：`thread_self`/`thread_equal`、`cpu_count`、`thread_yield`、`thread_set_name`；`ThreadPool` 的 `start(workers, jobs, cap)`（worker 上限 `POOL_MAX`=32）、`submit`（满时背压）/`try_submit`、`wait`（等空闲，之后还能继续投）、`stop`（join 全部 worker、幂等）、`pending`/`running`/`done`/`workers`/`is_started`；任务函数 `void (*fn)(void *)`，worker 是裸 pthread |
| `std/os/tty.c6` | `std.os.tty` | 终端（termios 薄封装，零分配）：`is_tty(fd)`、`size(fd, &rows, &cols)`/`width`/`height`/`set_size`（`TIOCGWINSZ`/`TIOCSWINSZ`）、`TtyMode`（`raw(fd)` 进原始模式、`restore()` 恢复、`is_active()`）；对非终端 `raw` 返回 0，失败不动输出参数 |
| `std/os/signal.c6` | `std.os.signal` | 信号（POSIX 薄封装）：`install(sig, handler)`（`sigaction` + `SA_RESTART`）、`ignore`/`reset`、`block`/`unblock`/`is_blocked`/`is_pending`（`sigprocmask`/`sigpending`）、`send_self`（`raise`）/`send_to`（`kill`）、`signal_name`/`signal_number`（20 个常用信号，带不带 `SIG` 前缀都认）；名字统一成动词，避免和 libc 的 `signal`/`raise`/`kill` 撞名 |
| `std/os/pipe.c6` | `std.os.pipe` | 匿名管道（`pipe(2)`）：`Pipe` 的 `init`/`init_nonblock`、`read_bytes`（`0`=EOF、`-1`=错误）、`read_exact`、`write_bytes`（返回实际字节数）、`set_nonblock`、`close_read`/`close_write`/`close_all`、`read_fd`/`write_fd`/`is_open`、`would_block()`；读写都重试 `EINTR`，fd 带 `FD_CLOEXEC` |
| `std/os/poll.c6` | `std.os.poll` | 多路等待（`poll(2)`）：`PollSet`（`POLL_MAX`=32 个槽，零分配）的 `init`/`add`（同 fd 改事件）/`modify`/`remove_fd`/`find`/`count`/`wait`（`<0` 永远等、`0` 立刻、`>0` 毫秒；返回就绪个数）/`is_ready`/`ready`/`fd_at`/`clear_ready`，单 fd 的 `wait_readable`/`wait_writable`/`wait_any`（1/0/-1）；事件位 `EVENT_READ`/`EVENT_WRITE`/`EVENT_ERROR`/`EVENT_HUP`/`EVENT_INVALID` |
| `std/os/time.c6` | `std.os.time` | `now_ns`/`now_ms`/`now_unix`/`now_unix_ms`、`sleep_ms`/`sleep_ns`、`Duration` 拆分与 `d.format(buf, cap)`、`Civil` 日历换算 `civil_from_unix`/`c.to_unix()`/`c.format(...)`，不依赖 localtime |

**`std/encoding/`**（编解码：Base64/十六进制/百分号、SHA-256、位流、UUID）

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `std/encoding/base64.c6` | `std.encoding.base64` | Base64（RFC 4648）标准表与 URL 安全表：`b64_encode`/`b64_decode`、`b64url_encode`/`b64url_decode`、显式表与填充的 `b64_encode_ex`/`b64_decode_ex`/`b64_decoded_len`/`b64_encoded_len`、`b64_valid`、就地 `b64_decode_inplace`/`b64url_decode_inplace`（跳过空白、严格填充、放不下不写半截） |
| `std/encoding/base32.c6` | `std.encoding.base32` | Base32（RFC 4648）：`b32_encode`/`b32_decode`/`b32hex_encode`/`b32hex_decode`、`b32_encode_ex`/`b32_decode_ex`（显式表与填充策略）、`b32_encoded_len`/`b32_decoded_len`/`b32_valid`；解码跳过空白、接受省略填充与小写，拒绝非零填充位 |
| `std/encoding/base85.c6` | `std.encoding.base85` | Ascii85（Adobe）与 Z85（ZeroMQ）：`a85_encode`/`a85_decode`、`a85_encoded_len`/`a85_decoded_len`/`a85_valid`（`z` 简写、部分组 n+1 字符、解码跳空白、`~>` 终止、溢出/非法字符拒绝）；`z85_encode`/`z85_decode`/`z85_valid`/`z85_encoded_len`/`z85_decoded_len`（长度必须 4/5 的倍数、严格字母表） |
| `std/encoding/base58.c6` | `std.encoding.base58` | Bitcoin Base58：`b58_encode`/`b58_decode`、`b58_encoded_len`（上界）/`b58_max_decoded_len`/`b58_valid`；前导零写 `1`，解码拒绝歧义字符 |
| `std/encoding/varint.c6` | `std.encoding.varint` | LEB128：`uvarint_encode`/`uvarint_decode`、`svarint_encode`/`svarint_decode`、`zigzag_encode`/`zigzag_decode`、`*_encoded_len`；解码返回 1/0/-1（成功/截断/非法） |
| `std/encoding/bitbuf.c6` | `std.encoding.bitbuf` | 位级读写器（零分配、MSB 先）：`bw_open`/`put`/`put_u64`/`put_bit`/`align`/`put_bytes`/`patch`（回填不改游标）、`br_open`/`get`/`get_u64`/`get_bit`/`peek`/`skip`/`align`/`get_bytes`、`bit_count`/`byte_count`/`ok`/`remaining`/`eof` |
| `std/encoding/codec.c6` | `std.encoding.codec` | 文本编码（写调用方缓冲）：Base64 `b64_encode`/`b64_decode`/`*_len`（接受省略填充与空白）、十六进制 `hex_encode`/`hex_decode`/`*_len`、百分号 `pct_encode`/`pct_decode`/`*_len`（`+` 可选） |
| `std/encoding/hexdump.c6` | `std.encoding.hexdump` | hexdump 文本与反向解析：`hex_opts`、`hexdump`/`hexdump_len`（width 1..64、`upper`、`ascii` 列、`base` 偏移；不足一行补空格、不可打印字节写 `.`）、`hexundump`（行首 8 位十六进制 + 空白当偏移，遇到 `|` 跳过 ASCII 列；奇数位/非法字符/放不下返回 -1） |
| `std/encoding/frame.c6` | `std.encoding.frame` | 串行分帧（零分配，写调用方缓冲）：COBS 的 `cobs_max_len`（上界）/`cobs_encoded_len`（精确）/`cobs_encode`/`cobs_decode`/`cobs_encode_frame`/`cobs_decode_frame`，SLIP（RFC 1055）的 `slip_encoded_len`/`slip_encode`/`slip_decode`/`slip_encode_frame`/`slip_decode_frame`；坏 code 字节、截断/非法转义、负载里的分隔符都拒绝，失败时不写半截 |
| `std/encoding/lz4.c6` | `std.encoding.lz4` | LZ4 块压缩/解压（零分配）：`lz4_compress`（调用方给 `int[lz4_table_size()]` 哈希表）/`lz4_decompress`/`lz4_compress_bound`；兼容标准 LZ4 块格式（结尾 5 字节字面量、最后匹配不早于结尾前 12 字节），坏偏移/截断/放不下都返回 -1 |
| `std/encoding/rle.c6` | `std.encoding.rle` | PackBits 游程（`rle_encode`/`rle_decode`/`rle_encoded_len`/`rle_decoded_len`/`rle_bound`，控制字节 0..127 字面量、129..255 重复、128 no-op，单段 ≤128）与一阶差分 `delta_encode`/`delta_decode`（模 256，和 RLE/LZ4 组合用） |
| `std/encoding/sha1.c6` | `std.encoding.sha1` | SHA-1 与 HMAC-SHA1（FIPS 180-4 / RFC 2104，零分配）：`sha1_new`/`init`/`update`/`final`、一次性 `sha1`/`sha1_hex`、`hmac_sha1`/`hmac_sha1_hex`、常数时间 `sha1_eq`；用于协议互操作（WebSocket 握手、git 对象名、HTTP Digest），抗碰撞请用 `std.encoding.sha256` |
| `std/encoding/sha256.c6` | `std.encoding.sha256` | SHA-256 与 HMAC-SHA256（FIPS 180-4 / RFC 2104）：`Sha256` 的 `init`/`update`/`final`、一次性 `sha256`/`sha256_hex`、`hmac_sha256`/`hmac_sha256_hex`、常数时间 `sha256_eq` |
| `std/encoding/punycode.c6` | `std.encoding.punycode` | Punycode（RFC 3492）：`punycode_encode`/`punycode_decode`/`punycode_encoded_len`/`punycode_decoded_len`；IDNA 的 A-label 层 `idna_encode`/`idna_decode`/`idna_encoded_len`/`idna_decoded_len`（`xn--`、Unicode 句点、63/255 字节上限） |
| `std/encoding/uuid.c6` | `std.encoding.uuid` | UUID（RFC 9562/4122）：`uuid_v4`/`Rand` 的 `v4_seeded`、`uuid_v7`/`uuid_v7_at`/`time_v7`、`v5` 与四个预定义命名空间 `uuid_ns_dns`/`uuid_ns_url`/`uuid_ns_oid`/`uuid_ns_x500`、`uuid_parse`、`Uuid` 的 `format`/`format_simple`/`format_upper`、`version`/`variant`/`eq`/`uuid_nil`/`is_nil` |

**`std/data/`**（数据格式：JSON、CSV、INI）

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `std/data/csv.c6` | `std.data.csv` | RFC 4180 CSV（零分配）：`CsvReader` 的 `csv_reader`/`next`/`rewind`（引号字段可跨行、CRLF、`""` 转义）、`csv_need_quote`/`csv_write_field`/`csv_write_row`（写调用方缓冲）、`csv_count_fields`/`csv_count_rows` |
| `std/data/ini.c6` | `std.data.ini` | INI 配置的零分配读取：`ini_open`/`next`（字段都是输入上的视图）、`ini_lookup`/`ini_has_section`/`ini_count`、`IniEntry.section`/`key`/`value`/`line`；语法错误给行号 |
| `std/data/props.c6` | `std.data.props` | `.properties` / `.env` 的零分配读取：`props_open` 工厂 + `PropsReader` 的 `next`、`PropEntry` 的 `decode`、`props_decode`/`props_decoded_len`（`\n`/`\uXXXX`/代理对/续行）、`props_lookup`/`props_has`/`props_count`；空键给行号 |
| `std/data/query.c6` | `std.data.query` | URL 查询串 / `application/x-www-form-urlencoded`（零分配）：`query_open` + `QueryReader.next`（跳过空段）、`QueryPair` 的 `name`/`value`/`has_eq`、`query_decode`（`+` → 空格、`%XX`、坏转义 -1）、`query_encode`/`query_encode_len`/`query_build`/`query_build_len`（`+` 表空格、其余 unreserved 原样）、`query_get`/`query_get_nth`/`query_has`（名字解码后比较） |
| `std/data/toml.c6` | `std.data.toml` | TOML 子集零分配读取（事件式）：`toml_open` + `TomlReader.next`（`TOML_TABLE`/`TOML_ARRAY_TABLE`/`TOML_KEY`）、`TomlEntry` 的 `kind`/`section`/`key`/`raw`/`line` 与 `decode`（基本字符串转义与 `\uXXXX`）、`toml_int`（十进制/0x/0o/0b/`_`/符号）、`toml_float`（委托 `std.text.num` 的 `parse_f64`，含 inf/nan）、`toml_bool`、`toml_array_len`/`toml_array_at`；语法错误给行号且能继续读 |
| `std/data/xml.c6` | `std.data.xml` | XML 零分配扫描（事件式，不做 DOM）：`xml_open` + `XmlReader.next`（`XML_START`/`XML_END`/`XML_SELF`/`XML_TEXT`）、`XmlEvent` 的 `name`/`text`/`attrs` 与 `attr_count`/`attr_at`/`attr`、`xml_unescape`（`&amp;` 等 5 个预定义实体 + `&#NNN;`/`&#xHH;`）；跳过注释/处理指令/DOCTYPE，CDATA 当文本，32 层标签配对校验，出错给字节位置 |
| `std/data/json.c6` | `std.data.json` | JSON（RFC 8259）解析与写出，DOM 住在 `std.core.mem` 的 `Arena` 上：`Json`（`JNULL`/`JBOOL`/`JNUM`/`JSTR`/`JARR`/`JOBJ`）、`Arena` 的 `json_parse`/`json_parse_at`、`Json` 的 `get`/`at`/`count`/`key`、`get_str`/`get_num`/`get_bool`、`write`/`write_pretty`/`json_escape`/`json_unescape`（数字保留原文、`\uXXXX` 与代理对解码、深度上限 64） |

**`std/net/`**（网络与 Web：IP/CIDR、URL、MIME）

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `std/net/inet.c6` | `std.net.inet` | IP 地址与 CIDR（零分配）：`inet_parse`/`inet_parse_v4`/`inet_parse_v6`，`Ip`/`Ipv4`/`Ipv6` 的 `format`（IPv6 按 RFC 5952 压缩）、`inet_parse_cidr`/`cidr_contains`/`cidr_mask`/`inet_v4_mask`、`is_private`/`is_loopback`/`is_link_local`/`is_multicast`/`is_unspecified`、`inet_parse_port` |
| `std/net/mime.c6` | `std.net.mime` | 扩展名 ↔ MIME 类型、分类与魔数嗅探（零分配）：`mime_of_ext`/`mime_of_path`/`mime_ext_of`/`mime_ext_at`/`mime_type_at`/`mime_count`、`mime_is_text`/`is_image`/`is_audio`/`is_video`/`is_font`、`mime_charset`、`mime_sniff`、`mime_default` |
| `std/net/http.c6` | `std.net.http` | HTTP/1.x 零分配解析：`http_parse_request`/`http_parse_response` 得到 `HttpMessage`，`HttpMessage` 的 `header`/`header_at`/`header_count`/`body`/`content_length`/`is_request`/`is_response`，`http_status_text`；头字段名大小写不敏感，不接受 obs-fold |
| `std/net/cookie.c6` | `std.net.cookie` | Cookie / Set-Cookie 零分配解析：`cookie_parse_set`/`cookie_parse_pair` 得到 `Cookie`，`Cookie` 的 `attribute`/`has_attribute`/`path`/`domain`/`expires`/`same_site`/`max_age`/`is_secure`/`is_http_only`、`cookie_write_pair` |
| `std/net/multipart.c6` | `std.net.multipart` | `multipart/form-data` 零分配解析（RFC 7578）：`mp_open`/`mp_open_boundary` 工厂 + `Multipart` 的 `next`/`count`、`Part` 的 `headers`/`disposition`/`name`/`filename`/`content_type`/`body`/`index`、`mp_header`/`mp_param`；伪分隔符当 preamble、无头 part、CRLF/LF 都认 |
| `std/net/dns.c6` | `std.net.dns` | DNS 报文（RFC 1035，零分配）：`dns_header`（id/flags/计数/rcode）、`dns_question`/`dns_rr`（压缩指针只允许向前指、最多 64 跳；`*next` 顺序遍历）、`dns_a`/`dns_aaaa`/`dns_cname`、`dns_build_query`（点分名字 → 查询报文）、`dns_type_name`/`dns_rcode_name`；不做 EDNS0/DoT/名字转义 |
| `std/net/tcp.c6` | `std.net.tcp` | TCP 套接字（POSIX 薄封装）：`tcp_listen`（port 0 = 内核挑）、`tcp_connect`（`timeout_ms >= 0` 用非阻塞 connect + `poll`）、`accept_client`、`Tcp` 的 `write_bytes`/`write_all`/`read_bytes`/`read_all`/`set_timeout`/`set_nodelay`/`shutdown_write`/`close_socket`/`port`/`ok`；getaddrinfo 解析主机（IPv4/IPv6/localhost），失败 `fd = -1` |
| `std/net/ws.c6` | `std.net.ws` | WebSocket（RFC 6455，零分配）：`ws_accept`（`base64(SHA1(key+GUID))`）、`ws_encode`/`ws_parse`/`ws_decode`（FIN/opcode、7/16/64 位长度、客户端掩码、u32 掩码键）、`ws_encode_len`、`WsFrame`；不重组分片（FIN 原样给出） |
| `std/net/url.c6` | `std.net.url` | URL（RFC 3986）解析与相对解析：`url_parse` 工厂 + `Url` 的 `effective_port`/`is_absolute`/`has_host`、`url_resolve`（§5.2 全算法）、查询参数 `next_pair`/`Pair`、百分号 `url_encode`/`url_encode_component`/`url_decode`/`url_decode_at`/`url_encoded_len`/`url_decoded_len` |

**集成示例**（`main` 只能写在模块外的文件里）

| 文件 | 模块 | 内容 |
| --- | --- | --- |
| `examples/wordcount.c6` | （无模块） | 把上面几块拼起来的示例程序：入口 `main` 只能写在**不属于任何模块**的文件里 |
| `examples/stdtour.c6` | （无模块） | 新模块的拼装示例（`make std-tour`）：strbuf/path/hash/bits/utf8/deque 各一节，自带自检 |
| `examples/stdkit.c6` | （无模块） | 第三批模块的拼装示例（`make std-kit`）：ascii/num/codec/rand/fs/set 各一节，自带自检 |
| `examples/stdbox.c6` | （无模块） | 第四批模块的拼装示例（`make std-box`）：json/sha256/csv/env 各一节，自带自检 |
| `examples/stdweb.c6` | （无模块） | 第五批模块的拼装示例（`make std-web`）：base64/url/uuid/mime 各一节，自带自检 |
| `examples/stdnet.c6` | （无模块） | 第六批模块的拼装示例（`make std-net`）：inet/bitbuf/pqueue/ini 各一节，自带自检 |
| `examples/stdmore.c6` | （无模块） | 第七批模块的拼装示例（`make std-more`）：bitset/glob/base32/http 各一节，自带自检 |
| `examples/stdnext.c6` | （无模块） | 第八批模块的拼装示例（`make std-next`）：base58/varint/semver/cookie 各一节，自带自检 |
| `examples/stdnode.c6` | （无模块） | 第九批模块的拼装示例（`make std-node`）：list/ring/utf16/ansi 各一节，自带自检 |
| `examples/stdform.c6` | （无模块） | 第十批模块的拼装示例（`make std-form`）：props/multipart/punycode/width 各一节，自带自检 |
| `examples/stdembed.c6` | （无模块） | 第十一批（嵌入式基础）的拼装示例（`make std-embed`）：fixed/crc/pool/frame 各一节，串成「采样 → 定点 → 分帧 → 校验 → 池」链路，自带自检 |
| `examples/stdtext.c6` | （无模块） | 第十二批（文本算法）的拼装示例（`make std-text`）：regex 拆日志、fuzzy 命令补全、wrap 帮助折行、diff 配置对比，自带自检 |
| `examples/stdnum.c6` | （无模块） | 第十三批（数值类型）的拼装示例（`make std-num`）：bignum 阶乘与进制、decimal 金额税额、rational 调和级数、complex 乘法与模，自带自检 |
| `examples/stdpack.c6` | （无模块） | 第十四批（压缩与文本编码）的拼装示例（`make std-pack`）：lz4 压缩遥测帧、rle+delta 压采样、base85 转文本、hexdump 文本化，自带自检 |
| `examples/stdconf.c6` | （无模块） | 第十五批（配置与文档格式）的拼装示例（`make std-conf`）：toml 读配置、xml 扫文档、query 解析/构建查询串、template 生成报告，自带自检 |
| `examples/stdcli.c6` | （无模块） | 第十六批（命令行工具基础）的拼装示例（`make std-cli`）：args 解析参数、natural 排序与人类可读化、table 排版、proc 跑子进程，自带自检 |
| `examples/stdwire.c6` | （无模块） | 第十七批（网络协议基础）的拼装示例（`make std-wire`）：sha1 向量、ws 握手/分帧、dns 查询与响应、loopback TCP 往返，自带自检 |
| `examples/stdalgo.c6` | （无模块） | 第十八批（算法与容器）的拼装示例（`make std-algo`）：依赖拓扑排序 + 最短路、LRU 缓存、命令前缀树、地址区间，自带自检 |
| `examples/stdthread.c6` | （无模块） | 第十九批（并发与同步）的拼装示例（`make std-thread`）：原子计数 + 自旋锁、互斥锁 + 条件变量握手、线程池跑任务、通道收发，自带自检 |
| `examples/stdterm.c6` | （无模块） | 第二十批（终端与进程控制）的拼装示例（`make std-term`）：伪终端检测/窗口大小/原始模式、信号处理与屏蔽、管道读写与 EOF、poll 多路等待，自带自检 |

`std/` 下每个 `.c6` 都自带 `test` 块，`make stdtest` 逐个跑，输出形如（数字会随模块增加变化，
以实际输出为准）：

```
std/container/bitset.c6    11 test(s), 0 failure(s)
std/container/deque.c6     9 test(s), 0 failure(s)
std/container/list.c6      4 test(s), 0 failure(s)
std/container/map.c6       9 test(s), 0 failure(s)
std/container/pqueue.c6    38 test(s), 0 failure(s)
std/container/range.c6     6 test(s), 0 failure(s)
std/container/ring.c6      2 test(s), 0 failure(s)
std/container/set.c6       17 test(s), 0 failure(s)
std/container/sort.c6      30 test(s), 0 failure(s)
std/container/span.c6      6 test(s), 0 failure(s)
std/container/vec.c6       42 test(s), 0 failure(s)
std/core/bits.c6           6 test(s), 0 failure(s)
std/core/crc.c6            3 test(s), 0 failure(s)
std/core/err.c6            3 test(s), 0 failure(s)
std/core/fixed.c6          4 test(s), 0 failure(s)
std/core/hash.c6           7 test(s), 0 failure(s)
std/core/math.c6           6 test(s), 0 failure(s)
std/core/mem.c6            7 test(s), 0 failure(s)
std/core/pool.c6           2 test(s), 0 failure(s)
std/core/rand.c6           9 test(s), 0 failure(s)
std/core/types.c6          4 test(s), 0 failure(s)
std/data/csv.c6            41 test(s), 0 failure(s)
std/data/ini.c6            6 test(s), 0 failure(s)
std/data/json.c6           60 test(s), 0 failure(s)
std/data/props.c6          4 test(s), 0 failure(s)
std/encoding/base32.c6     5 test(s), 0 failure(s)
std/encoding/base58.c6     3 test(s), 0 failure(s)
std/encoding/base64.c6     8 test(s), 0 failure(s)
std/encoding/bitbuf.c6     7 test(s), 0 failure(s)
std/encoding/codec.c6      7 test(s), 0 failure(s)
std/encoding/frame.c6      4 test(s), 0 failure(s)
std/encoding/punycode.c6   11 test(s), 0 failure(s)
std/encoding/sha256.c6     23 test(s), 0 failure(s)
std/encoding/uuid.c6       24 test(s), 0 failure(s)
std/encoding/varint.c6     4 test(s), 0 failure(s)
std/net/cookie.c6          4 test(s), 0 failure(s)
std/net/http.c6            5 test(s), 0 failure(s)
std/net/inet.c6            8 test(s), 0 failure(s)
std/net/mime.c6            37 test(s), 0 failure(s)
std/net/multipart.c6       4 test(s), 0 failure(s)
std/net/url.c6             32 test(s), 0 failure(s)
std/os/env.c6              39 test(s), 0 failure(s)
std/os/fs.c6               7 test(s), 0 failure(s)
std/os/io.c6               5 test(s), 0 failure(s)
std/os/path.c6             30 test(s), 0 failure(s)
std/os/time.c6             7 test(s), 0 failure(s)
std/text/ansi.c6           4 test(s), 0 failure(s)
std/text/ascii.c6          5 test(s), 0 failure(s)
std/text/fmt.c6            10 test(s), 0 failure(s)
std/text/glob.c6           6 test(s), 0 failure(s)
std/text/num.c6            11 test(s), 0 failure(s)
std/text/semver.c6         4 test(s), 0 failure(s)
std/text/strbuf.c6         18 test(s), 0 failure(s)
std/text/str.c6            22 test(s), 0 failure(s)
std/text/utf16.c6          4 test(s), 0 failure(s)
std/text/utf8.c6           7 test(s), 0 failure(s)
std/text/width.c6          10 test(s), 0 failure(s)
```

`-test` 的计数是命令行上**所有 TU** 的 `test` 块之和，所以带依赖跑的行会把自己
和依赖的块一起算进去，于是比模块本身的块数多：

| 行 | 构成 |
| --- | --- |
| `std/container/sort.c6` 30 | str 22 + sort 8 |
| `std/container/vec.c6` 42 | str 22 + sort 8 + vec 12 |
| `std/os/path.c6` 30 | str 22 + path 8（path 只借 `Str` 这个类型，函数体不依赖 str 的代码） |
| `std/text/strbuf.c6` 18 | fmt 10 + strbuf 8 |
| `std/container/set.c6` 17 | map 9 + set 8 |
| `std/data/json.c6` 60 | json 17 + str 22 + num 11 + mem 7 + err 3 |
| `std/data/csv.c6` 41 | csv 19 + str 22（csv 只借 `Str` 类型，函数体不依赖 str 的代码） |
| `std/os/env.c6` 39 | env 14 + str 22 + err 3（env 用到 `str()`） |
| `std/encoding/sha256.c6` 23 | sha256 17 + bits 6（`rotr32` 是跨 TU 调用） |
| `std/net/url.c6` 32 | url 10 + str 22（url 只借 `Str` 类型，正文不调用 str） |
| `std/net/mime.c6` 37 | mime 7 + str 22 + path 8（取扩展名要 `std.os.path` 的正文） |
| `std/encoding/uuid.c6` 24 | uuid 8 + rand 9 + time 7（v4/v7 要随机源，v7 要墙钟） |
| `std/container/pqueue.c6` 38 | pqueue 8 + sort 30（比较器来自 `std.container.sort`；`cmp_str` 又拉上 `std.text.str`，但那样跑才有 str 的块参与） |
| `std/container/bitset.c6` 11 | bitset 5 + bits 6（`popcount64`/`ctz64` 是跨 TU 调用） |
| `std/text/width.c6` 10 | utf8 7 + width 3（判宽要 `utf8_decode`） |
| `std/encoding/punycode.c6` 11 | utf8 7 + punycode 4（编码/解码都走 `utf8_decode`） |
| `std/container/chan.c6` 10 | chan 4 + sync 6（Mutex/Cond 的正文来自 `std.os.sync`） |

`std/core/bits`、`std/core/crc`、`std/core/fixed`、`std/core/pool`、`std/core/hash`、`std/text/utf8`、`std/container/deque`、`std/text/ascii`、`std/text/num`、
`std/encoding/codec`、`std/encoding/frame`、`std/core/rand`、`std/os/fs`、`std/encoding/base64`、`std/text/glob`、`std/encoding/base32`、`std/encoding/base58`、`std/encoding/varint`、`std/text/semver`、`std/net/cookie`、`std/net/inet`、`std/net/http`、`std/encoding/bitbuf`、`std/container/list`、`std/container/ring`、`std/text/utf16`、`std/text/ansi`、
`std/data/props`、`std/net/multipart`
自成一体，单独跑就是自己的块数（props/multipart 只借 `Str` 这个类型）；`std/data/csv`、`std/net/url`、`std/data/ini` 只有声明依赖
`std.text.str`（正文不调用），`std/net/mime` 的正文必须带 `std/os/path`，`std/encoding/sha256` 的正文
必须带 `std/core/bits`，`std/os/env` 的正文必须带 `std/text/str`，`std/encoding/uuid` 的正文必须带
`std/core/rand` 与 `std/os/time`，`std/container/pqueue` 的正文必须带 `std/container/sort`，`std/text/width` 与
`std/encoding/punycode` 的正文必须带 `std/text/utf8`。

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
import std.core.types;
import std.core.err;

u32 bytes = 0;
i64 total = 0;

if (!is_ok(code))
    printf("错误：%s\n", msg_of(code));       /* 或 msg(code) */
```

`std.core.err.Error` 是普通枚举，模块外写 `std.core.err.NOMEM`，模块内写 `NOMEM`。
std 模块的号段内置在编译器里（`std.core.err` = `0x6000`），所以 `std.core.err.NOMEM`
的真实值是 `0x0E000003`：

```
id = (area << 27) | (slot << 12) | code     // area=1(std), slot=0x6000, code=3
```

这个号在**任何**程序里都一样，不需要 `errbase.def`，也永远不参与自动池的
哈希与撞号检查；std 模块的号段表见 [grammar.md](reference/grammar.md) §8.1。

### `Vect@T`：托管容器

```c
import std.container.vec;

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
import std.container.span;

int a[5] = { 1, 2, 3, 4, 5 };
Span@int s = { a };                      /* 聚合初始化：视图就是数组本身 */
Span@int *p = &s;                        /* 值容器，方法要走指针 */

int n = p->count();                      /* 5 */
Span@int t = p->sub(1, 3);               /* { 2, 3, 4 }，不复制 */
printf("%d\n", (&t)->at(0));
```

`Span` 是纯值（一个切片），所以能按值声明；但方法接收者是指针，
值容器调用写 `(&s)->m()`。`sub()`/`drop()`/`take()` 只调整边界，零拷贝。

### `Bitset`：借用 `u64` 缓冲的位集合

```c
import std.container.bitset;

u64 words[2] = { 0 };                    /* 70 位只需要 2 个 u64 */
Bitset b = bitset_make(words, 70);       /* 不分配，视图借用 words */
b.set(0) catch e { return e.id; };
b.set(69) catch e { return e.id; };

b.count();          /* 2 */
b.bit(69);          /* 1 */
b.first_set();      /* 0 */
b.next_set(1);      /* 69 */
b.set_all();  b.clear_all();
b.union_with(&other) catch e { return e.id; };
```

`Bitset` 只包一个 `u64 *` 和位数，和 `Span` 一样是纯值视图：`words` 必须比它活得久。
写操作（`set`/`clear`/`toggle`）越界返回 `INVAL`，读操作（`bit`/`next_set`/`first_set`）
越界给 0 / -1；最后一个字里超出 `size()` 的位始终是 0，所以 `count()`、`equals()` 和
集合运算都不需要再掩码。集合运算要求两边 `size()` 相同，失败时不改接收者。

### `List`：侵入式双向链表

```c
import std.container.list;

List l = { 0 };
ListNode a = { 0 }, b = { 0 };
a.data = "a";  b.data = "b";
l.push_back(&a);  l.push_back(&b);
l.count();                 /* 2 */
l.first()->data;           /* "a" */
l.pop_front();             /* &a */
```

链表不拥有节点：`ListNode` 由调用方声明，`List` 只维护 `head`/`tail`/`n`，
push/pop/remove 只改指针、不分配。`clear` 把节点全部摘下（next/prev 置 0）但不动
`data`；`remove` 先 O(n) 确认节点在本链表里，避免误删别的链。一个节点同一时刻
只能属于一个链表，已经链在别处的节点再 push 会让原链断掉，库不检测。
`ListIter` 提供 `has`/`next`，可以手写遍历。

### `Ring`：固定容量环形队列

```c
import std.container.ring;

void *slots[4] = { 0 };
Ring r = ring_make(slots, 4);
r.push(&a);  r.push(&b);
r.pop(&out);               /* 1, out == &a */
r.peek(&out);              /* 看最旧，不出队 */
r.at(0, &out);             /* 从旧到新取第 0 个 */
```

和 `Span`/`Bitset` 一样是纯值视图：借用调用方数组，不分配。满了 `push` 返回 0，
**不覆盖旧值**；要覆盖先 `pop` 或查 `is_full()`。`pop`/`peek`/`at` 用输出参数返回
`void *`，所以元素本身是 0 也能和「空」区分。

### `Map@(K, V)`：开放寻址哈希表

```c
import std.container.map;

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
已删除的键又出现。这个坑是写 `std/container/set` 时踩到的（Set 的 add/remove 会留下墓碑，
再插入就触发扩容），`std/container/map.c6` 的用例「墓碑不会在扩容时复活」钉住了它。

### `Str`：字符串视图

```c
import std.text.str;

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
`count_sub()` / `trim_left()` / `trim_right()` / `hash()`（FNV-1a，和 `std.container.map`
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

### `glob`：shell 风格模式匹配

```c
import std.text.glob;

Str pat = { S("src/*.c6") };
Str path = { S("src/main.c6") };
Glob g = glob(pat);

g.match(path);        /* 1：* 可以跨 / */
g.match_path(path);   /* 1：* 和 ? 不跨 / */
g.has_magic();        /* 1 */
glob_match(pat, path);   /* 一次性入口 */
```

支持 `*`、`?`、`[abc]`、`[a-z]`、`[!a-z]`（`^` 同义）和 `\` 转义；大小写敏感，
不做 locale。没有 `**` 的特殊语义，两个星号等价于一个。坏字符类（没有结尾 `]`）
退回字面量 `[`，不报错——glob 是匹配器，不是语法检查器；要判断有没有通配符用
`has_magic()`。

### `semver`：语义化版本 2.0.0

```c
import std.text.semver;

Str raw = { S("1.2.3-rc.1+build.7") };
Semver v = semver_parse(raw);

v.ok;  v.major;  v.minor;  v.patch;
v.pre;  v.build;                 /* 输入上的视图 */
v.is_prerelease();               /* 1 */
v.compare(&other);               /* -1 / 0 / 1 */
v.format(buf, sizeof buf);       /* "1.2.3-rc.1+build.7" */
```

严格按 semver.org：数字标识不能有前导零，预发布/构建标识非空且只含
`[0-9A-Za-z-]`；预发布里纯数字标识按数值比较、数字小于非数字，构建标识不参与
优先级。解析失败时 `ok == 0`，`err` 是 `PARSE` 或 `INVAL`。

### `utf16`：UTF-16 解码与编码

```c
import std.text.utf16;

u16 units[5] = { 'h', 'i', 0xd83d, 0xde00, '!' };  /* "hi😀!" */
Utf16 s = utf16((u16[:]){ units, 5 });
int pos = 0;
u32 cp = 0;

s.valid();                 /* 1 */
s.count();                 /* 4 */
s.decode(&pos, &cp);       /* 1, 'h' */
pos = 2;
s.decode(&pos, &cp);       /* 1, U+1F600, pos == 4 */

u16 out[2] = { 0 };
utf16_encode(0x1f600u, out, 2);   /* 2, { 0xd83d, 0xde00 } */
```

按 UTF-16 代码单元处理：代理对解码成一个码点，落单代理或高代理后接非低代理返回
-1，代理对中间截断返回 0；`count` 遇非法序列给 -1。编码只接受 <= U+10FFFF 且不在
代理区的码点，失败不写任何单元。不做 BOM，也不换字节序（`u16` 是宿主端序）。

### `ansi`：ANSI 转义构造器

```c
import std.text.ansi;

char buf[64] = { 0 };
Ansi a = ansi_make(buf, sizeof buf);

a.reset();          /* ESC[0m */
a.bold();           /* ESC[1m */
a.fg_basic(1);      /* ESC[31m */
a.move(2, 3);       /* ESC[2;3H */
a.fg(196);          /* ESC[38;5;196m */
puts(a.cstr());     /* 末尾补 NUL，可直接输出 */
```

只拼字节，不判断终端、不写 stdout；`cstr()` 在末尾补 NUL（需要多一格）。缓冲
放不下时把 `ok` 置 0，后续写入全部忽略；失败时可能留下半条序列，所以先看 `ok`
再用。`fg_basic`/`bg_basic` 是 8 色，`fg`/`bg` 是 256 色；光标移动的 row/col 从 1 起。

### `Buf@(N)`：定长格式化

```c
import std.text.fmt;

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
`-1` 且一个字节都不写。`Buf@(N)` 的 `write_*` 和 `std.text.strbuf` 都建在它们
上面，所以两条路径的格式永远一致。`write_hex` 收 `u32`，要写满 64 位十六进制
用 `write_hex64`（两个入口都只是 `digits_hex` 的薄包装）。曾经把 `write_hex`
直接加宽到 `u64` 会挂住编译器，现在**复现不出来**了（见下面的「已知限制」），
保留一个 `u32` 入口只是不想为改签名动 API。

### `StrBuf`：会自己长大的字符串

```c
import std.text.strbuf;

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

/* 方法名就是 write / count / cstr …；普通结构体（体内写不了方法体）的方法
   写在体外，模块里的裸名弱别名带模块前缀，不会和别的模块抢符号 */
```

和 `Buf@(N)` 的分工：上限是常量（协议头、日志行）就用 `Buf@(N)`，纯值、零
分配；长度事先不知道才用 `StrBuf`。扩容从 64 起翻倍，超过 `0x3fffffff` 直接
报 `NOMEM`（不先分配再失败）。

### `path_*`：POSIX 路径拆装

```c
import std.os.path;
import std.text.str;                          /* 只为 Str 这个类型 */

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
import std.core.hash;
import std.core.bits;
import std.text.utf8;

u32 h = fnv1a_cstr("docs/stdlib.md");    /* 与 Str::hash、std.container.map 同算法 */
u32 c = crc32_bytes("123456789", 9);     /* 0xCBF43926 */
u64 h64 = fnv1a64_bytes(p, n);

Hasher st = hasher();                    /* 流式：分几段喂结果一样 */
st.update(s1.data, (usize)s1.len);
u32 d = st.digest();

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
import std.container.deque;

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
import std.text.ascii;

ascii_is_digit('7');  ascii_is_alpha('A');  ascii_is_space('\t');
ascii_hex_value('F');            /* 15；非法给 -1 */
ascii_to_lower('Q');             /* 'q' */
ascii_cmp_nocase(S("Hello"), S("hello"));   /* 0 */

char buf[8] = "aBc";
ascii_upper(buf, 3);             /* 原地 -> "ABC" */
```

参数收 `int` 而不是 `char`：`fgetc()` 的 EOF(-1) 和带符号 char 的负值都安全，
`c < 0 || c > 127` 一律判为「不是」，转换函数原样返回。只认 ASCII、不看
locale——多字节是 `std.text.utf8` 的事。`ascii_upper`/`ascii_lower` 是这一层唯一的
「写」函数，原地改调用方缓冲；要不改原内存的大小写转换用 `std.text.str` 的
`copy_upper`/`copy_lower`。

### `num`：浮点的格式化与解析

```c
import std.text.num;

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
import std.encoding.codec;

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

和 `std.os.path`/`std.text.fmt` 同一个约定：**不分配**，写调用方缓冲，放不下返回 -1
且一个字节都不写。解码器先完整校验一遍再写第二遍，非法输入不会留下半截：
Base64 接受省略填充和 ASCII 空白，拒绝 `% 4 == 1`、非法字符、`=` 在中间；
十六进制要求偶数个字符、不跳空白；百分号拒绝截断的 `%X`。`*_encoded_len`/
`*_decoded_len` 可以先问长度。

### `rand`：确定性伪随机

```c
import std.core.rand;
import std.os.time;

Rand r = rand_seed(now_ns());           /* 纯值，可重放：同一个种子同一串数 */
u64 x = r.next_u64();                   /* 下一个 64 位 */
int d6 = r.int_range_incl(1, 6);        /* [1, 6] */
u64 b = r.below(100);                   /* [0, 100)，拒绝采样，无余数偏置 */
f64 p = r.next_f64();                   /* [0, 1) */

int a[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
r.shuffle_int((int[:]){ a, 10 });
```

算法是 xorshift64*（只用 u64 的异或/移位/乘），种子 0 换成非零常量（全零状态
是吸收态）。**不是密码学随机数**：密钥、令牌、盐不要用它。`shuffle` 按
字节交换，任意元素宽度都能用（`elem` 给 `sizeof`）。已知序列钉在
`std/core/rand.c6` 的 `test` 块里。

### `fs`：文件系统元数据与目录

```c
import std.os.fs;

StatInfo st = { 0 };
fs_stat("docs/stdlib.md", &st) catch e { return e.id; };
st.is_file;  st.size;  st.mtime;

fs_mkdir_p("build/out/a", 0755) catch e { return e.id; };

Dir d = dir_open("std");
Dir *pd = &d;
char name[256] = { 0 };
int is_dir = 0;
while (pd->next(name, sizeof name, &is_dir) > 0) { ... }
pd->close();
```

`std.os.io` 管内容、`std.os.fs` 管元数据和目录。`errno` 被翻译成 `std.core.err` 的码：
不存在 → `NO_ENT`、已存在 → `EXIST`、参数/不是目录/没权限 → `INVAL`、其余 →
`IO`。`next` 返回 1 取到、0 结束、-1 出错、**-2 名字比 cap 长**（不截断，
让调用方加大 buffer）；"."/".." 照常返回。`is_dir` 取自 `dirent.d_type`，
`DT_UNKNOWN` 时给 0。这一层依赖 POSIX，freestanding 目标用不了（io 至少只要
stdio）。

### `set`：哈希集合

```c
import std.container.set;

@Set@int s = new Set@int;
s->add(7) catch e { return e.id; };   /* 返回值：1 本次新增、0 已经存在 */
s->has(7);  s->remove(7);  s->count();
foreach (int k = s->iter()) { ... }
```

键的要求和 `std.container.map` 完全一致（按 `sizeof(K)` 个字节做 FNV-1a、memcmp 判等），
`Set` 就是 `@Map@(K, int)` 的薄包装——所以**用 Set 的程序要把 `std/container/map.c6` 的
正文一起编**。`add` 会分配、返回 `int!error`（失败不改集合）；`remove`/`has`
不失败；`iter()` 借用集合，改集合之前先迭代完。

### `json`：arena 上的 DOM

```c
import std.data.json;
import std.core.mem;
import std.text.str;

#define S(lit) ((char[:]){ (char *)(lit), (int)(sizeof(lit) - 1) })

char pool[4096] = { 0 };                     /* DOM 住在这块内存里，不 malloc */
Arena a = arena_make(pool, (int)sizeof pool);
Str text = { S("{\"n\": 3, \"ok\": true}") };  /* char[:] 不会隐式转成 Str */
Json root = a.json_parse(text) catch e { return e.id; };
Json *p = &root;

Json *n = p->get("n");                  /* 找不到 / 不是对象给 0 */
int ok = p->get_bool("ok", 0);
f64 v = p->get_num("n", 0.0);
char out[64] = { 0 };
p->write(out, sizeof out);               /* {"n":3,"ok":true} */
```

`Json` 是纯值结构体，解析出来的整棵树都由传进去的 `Arena` 分配：`reset` 一次
全部释放，不碰 `malloc`、不引入托管引用（所以 `std.data.json` 在 freestanding 目标
也能用，只要 `std.core.mem` 的 alloc 还在）。字符串值解码进 arena，键和值都是指向
arena 的 `Str` 视图；`json_parse` 失败时消息是静态描述，要定位就换
`a.json_parse_at(text, &pos)`（多一个 `int *err_pos`，写字节偏移）。

语义钉死的几处：

* **严格 RFC 8259**：数字只认 `-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?`
  （`+1`、`.5`、`01`、`1.`、`nan` 都拒绝）；字符串里未转义的控制字符报
  `PARSE`；`\uXXXX` 解码成 UTF-8，代理对合并、落单代理报 `PARSE`；顶层任意
  值；允许开头一个 UTF-8 BOM；尾随垃圾报 `PARSE`。
* **数字不丢精度**：`JNUM` 同时保留 `raw`（输入原文）与 `num`（f64）。写出时
  优先 `raw`，所以 `9007199254740993` 原样往返；没有 `raw` 的（用户自己构造的
  节点）用 `std.text.num` 的 `fmt_f64_g` 现算（有效位数按十进制指数收敛到 11–17 位，
  `inf`/`nan` 写成 `null`，因为 JSON 没有这两个字面量）。
* **递归上限 64 层**，超过给 `RANGE`；空输入/只有空白给 `INVAL`；arena 满给
  `NOMEM`。
* `get` 对重复键返回**最后**一个；`count`/`at`/`key` 只在
  对应容器上有效。
* 写函数都是「先算长度再写」：容量不够返回 -1 且一个字节都不写；
  `write_pretty(..., indent)` 每层缩进 `indent` 个空格（`<= 0` 当 2）。

### `sha256`

```c
import std.encoding.sha256;

u8 dig[32] = { 0 };
char hex[65] = { 0 };
Sha256 st = { { 0 }, 0, { 0 }, 0 };

sha256("abc", 3, dig);                       /* 一次性 */
sha256_hex("abc", 3, hex);                   /* "ba7816bf…15ad"，小写 + NUL */

st.init();                            /* 流式：任意切分结果相同 */
st.update("a", 1);
st.update("bc", 2);
st.final(dig);

hmac_sha256(key, klen, msg, mlen, dig);      /* RFC 2104 */
sha256_eq(dig, other32);                     /* 常数时间比较，1/0 */
```

FIPS 180-4 的纯实现：只用 `memcpy`/`memset`，不分配、不链 libm、不碰宿主加密
库。`update` 支持 1 字节一喂和任意块边界（63/64/65 都测过），
`final` 之后想复用就再 `init`。HMAC 的 key 超过 64 字节先哈希，ipad/opad
在栈上算。已知向量（空串、`"abc"`、两块的 NIST 串、一百万 `a`，以及 RFC 4231
的 HMAC 用例 1/2/6）全部钉在 `std/encoding/sha256.c6` 的 `test` 块里。

### `csv`

```c
import std.data.csv;
import std.text.str;

Str text = { S("name,qty\nbolt,12\n\"M6, hex\",3\n") };
CsvReader r = csv_reader(text);               /* 纯值：按值声明、按值传 */
CsvReader *p = &r;
Str f = { { (char *)0, 0 } };

while (p->next(&f) > 0) {                 /* 1 有字段、0 结束、-1 格式错 */
    /* f 是输入里的视图；p->row / p->col / p->at_eol 告诉你位置 */
    if (p->at_eol)
        putchar('\n');
}
if (p->error)
    printf("csv: %d\n", p->error);             /* 本模块的码，不是 std.core.err 的 */

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

`p->error` 里存的是**本模块**的码（`std.data.csv.PARSE` / `std.data.csv.INVAL`），不是
`std.core.err` 的码，所以别直接丢给 `std.core.err.msg()`。

### `env`：环境变量与命令行

```c
import std.os.env;

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
import std.encoding.base64;

char enc[64] = { 0 };
char dec[64] = { 0 };

b64_encode(S("foobar"), enc, sizeof enc);        /* 8，"Zm9vYmFy" */
b64url_encode(S("\xfb\xff"), enc, sizeof enc);   /* 3，"-_8"（不带填充） */
b64_decode(S("Zm9vYmFy"), dec, sizeof dec);      /* 6，"foobar" */
b64url_decode(S("-_8"), dec, sizeof dec);        /* 2 */

b64_encoded_len(6, 1);                           /* 8：先问长度再给缓冲 */
b64_decoded_len(S("Zm9vYmFy"), 0, 1);            /* 6；非法输入给 -1 */
```

`std.encoding.codec` 里那份 Base64 是**宽容**口径（跳过空白、接受省略填充）。这一层是给
协议实现用的，表和填充都是**显式参数**，因为 JWT/JWKS 与 HTTP 头的要求不一样：

* `url` 参数：0 = 标准表（`+` `/`），1 = URL 安全表（`-` `_`）；两张表**互不混用**；
* `pad` 参数：1 = 输出填充 / 要求填充，0 = 不输出 / 不要求。

```c
b64_encode_ex(S("f"), enc, sizeof enc, 0, 0);    /* 2，"Zg"——不带 `=` */
b64_decode_ex(S("Zg"), dec, sizeof dec, 0, 0);   /* 1：pad = 0 时才接受省略 */
b64_decode(S("Zg"), dec, sizeof dec);            /* -1：pad = 1 缺填充就报错 */
b64url_decode(S("Zg=="), dec, sizeof dec);       /* -1：URL 变体下 `=` 是非法字符 */
```

**编码不写结尾 NUL**（和 `std.encoding.codec` 一致，长度就是返回值）；要当 C 字符串用就多给
一格。`*_decode_inplace` 是例外：它在同一块内存上解码，**会补一个 NUL**（否则输入
剩下的编码字符会留在结果尾巴上）。解码一律先完整校验再写第二遍，非法输入与「放
不下」都不留半截结果。

### `base32`：标准表与 base32hex

```c
import std.encoding.base32;

char out[32] = { 0 };
char back[32] = { 0 };

b32_encode(S("foobar"), out, sizeof out);          /* 16, "MZXW6YTBOI======" */
b32_decode(S("MZXW6YTBOI"), back, sizeof back);    /* 6, "foobar"：默认宽容 */
b32hex_encode(S("foo"), out, sizeof out);          /* 8, "CPNMU===" */
b32_decode_ex(S("MY"), back, sizeof back, 0, 1);   /* -1：严格要求填充 */
```

和 `base64` 同一口径：不分配，写调用方缓冲，放不下返回 -1 且不写半截；解码先完整
校验再写第二遍。`pad = 1` 编码补 `=`、解码要求填充；`pad = 0` 编码不补、解码两种
都收。解码一律跳过 ASCII 空白，接受小写，拒绝非零填充位和凑不出整字节的长度。

### `base58`：Bitcoin 风格的大数编码

```c
import std.encoding.base58;

char out[64] = { 0 };
char back[64] = { 0 };

b58_encode(S("hello world"), out, sizeof out);   /* 15, "StV1DL6CwTryKyV" */
b58_decode(S("StV1DL6CwTryKyV"), back, sizeof back); /* 11, "hello world" */
b58_encode(S("\0\0\1"), out, sizeof out);          /* 3, "112"（前导零写 1） */
```

字母表是 Bitcoin 的 `123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz`，
去掉了 `0/O/I/l`。编码把输入当大端整数反复除以 58，前导零字节写成前导 `1`；
解码拒绝字母表外的字符。`b58_encoded_len` 是上界（精确长度要真做一遍大数除法），
按 `b58_max_decoded_len` 给解码缓冲即可；两个方向都是放不下返回 -1 且不写半截。

### `varint`：LEB128 与 zigzag

```c
import std.encoding.varint;

char buf[10] = { 0 };
u64 v = 0;
i64 sv = 0;
int pos = 0;

int n = uvarint_encode(300, buf, sizeof buf);     /* 2, { 0xac, 0x02 } */
uvarint_decode((char[:]){ buf, n }, &pos, &v);    /* 1, v == 300, pos == 2 */

n = svarint_encode(-1234567890123ll, buf, sizeof buf);
pos = 0;
svarint_decode((char[:]){ buf, n }, &pos, &sv);   /* zigzag：小负数也短 */
```

无符号数最多 10 字节；解码返回 1（成功）、0（输入截断）、-1（非法/溢出/参数错），
只有成功才更新 `*pos` 与 `*out`。`zigzag_encode`/`zigzag_decode` 把有符号数映射到
无符号（0→0、-1→1、1→2、-2→3）。

### `url`：RFC 3986 的分解、相对解析与百分号

```c
import std.net.url;

Url u = url_parse(S("https://user:pw@example.com:8443/a/b?q=1&r=2#frag"));
u.ok;  u.scheme;  u.userinfo;  u.host;  u.port;  u.path;  u.query;  u.fragment;
u.effective_port();            /* 8443；没写端口时按 scheme 给 80/443/21/23 */

char out[256] = { 0 };
url_resolve(S("http://a/b/c/d;p?q"), S("../g"), out, sizeof out);   /* "http://a/b/g" */

Pair p = { { { (char *)0, 0 } }, { { (char *)0, 0 } } };
int at = 0;
while (u.query.next_pair(&at, &p))
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

### `http`：HTTP/1.x 请求与响应解析

```c
import std.net.http;

Str raw = { S("POST /api HTTP/1.1\r\nHost: example.com\r\n"
              "Content-Length: 2\r\n\r\nhi") };
Str host = { S("host") };
HttpMessage req = http_parse_request(raw);

req.ok;                 /* 1 */
req.method;             /* "POST" */
req.target;             /* "/api" */
req.header(host);       /* "example.com"，名字大小写不敏感 */
req.header_count();     /* 2 */
req.content_length();   /* 2 */
req.body();             /* "hi" 的视图 */
```

解析是**零分配**的：起始行、每个头字段和 body 都是输入上的视图；只处理已经收完的
字节，不碰 socket。起始行和头字段接受 CRLF 也接受裸 LF；请求行是
`METHOD SP target SP HTTP/x.y`，状态行是 `HTTP/x.y SP 3 位状态码 SP reason`。
头字段名必须是 token，值去掉首尾 SP/HTAB，不接受 obs-fold（以空格/HTAB 开头的
续行）。失败时 `ok == 0`，`err` 是 `PARSE` 或 `INVAL`。`content_length()` 只做
十进制语法解析，不按它截断 `body()`。

### `cookie`：Cookie 与 Set-Cookie

```c
import std.net.cookie;

Str raw = { S("sid=abc123; Path=/app; Max-Age=3600; Secure; "
              "HttpOnly; SameSite=Lax") };
Cookie c = cookie_parse_set(raw);

c.name;  c.value;                /* "sid", "abc123" */
c.path();                        /* "/app" */
c.is_secure();  c.is_http_only();/* 1, 1 */
c.same_site();                   /* "Lax" */
int age = 0;
c.max_age(&age);                 /* 1, age == 3600 */

Str name = { 0 }, value = { 0 };
cookie_parse_pair(S("a=b; c=d"), &name, &value);   /* 1, "a", "b" */
cookie_write_pair(c.name, c.value, buf, sizeof buf); /* "sid=abc123" */
```

属性名大小写不敏感，`Secure`/`HttpOnly` 这类没有值的旗标也算存在；`attribute()`
可以取任意属性。所有字段都是输入上的视图，不分配。这一层不处理引号里的分号
（`value="a;b"` 会被当成两段）——需要完整 RFC 6265 引号语法时先自己拆好。

### `uuid`：v4 / v5 / v7

```c
import std.encoding.uuid;

Uuid a = uuid_v4();                            /* 随机 */
Uuid b = uuid_v7();                            /* 时间排序：前 48 位是 Unix 毫秒 */
Uuid ns = uuid_ns_dns();
Uuid c = ns.v5(S("www.example.com"));   /* 2ed6657d-e927-568b-95e1-2665a8aea6a2 */

char buf[40] = { 0 };
a.format(buf, sizeof buf);                 /* 36 字符 + NUL */
a.format_simple(buf, sizeof buf);       /* 32 字符，不带连字符 */
Uuid back = { { 0 } };
uuid_parse(S("6ba7b810-9dad-11d1-80b4-00c04fd430c8"), &back);   /* 1 */

c.version();      /* 5 */
c.variant();      /* 1 = RFC 4122 */
b.time_v7();      /* 版本 7 里那 48 位毫秒时间戳 */
```

`Uuid` 是 16 字节的**纯值**（`struct Uuid { u8 b[16]; }`），可放进别的结构体、按
字节比较。三个生成器的取舍：

| 生成器 | 随机源 | 用途 |
| --- | --- | --- |
| `uuid_v4` | 时钟 + 栈地址播种的 `std.core.rand` | 通用唯一标识；**不是密码学随机** |
| `uuid_v7` | Unix 毫秒 + 随机位 | 数据库主键之类「想按时间聚簇」的场合（同毫秒内不保证严格单调） |
| `v5` | 无（SHA-1 散列） | 同一命名空间 + 名字永远给同一个 UUID；SHA-1 只在文件内部实现，不链加密库 |

要可重现的用例用 `r.v4_seeded()` / `uuid_v7_at(ms, &r)`。`uuid_parse` 接受
36 字符、32 字符、带 `urn:uuid:` 前缀与 `{}` 包装的形式，大小写不敏感；坏输入返回 0
且**不动 `*out`**。

### `mime`：扩展名 ↔ 类型、分类与魔数

```c
import std.net.mime;

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
import std.net.inet;

Ip a = { 0 };
char buf[64] = { 0 };

inet_parse(S("192.168.0.1"), &a);          /* 1；a.family = IP_V4 */
inet_parse(S("2001:db8::1"), &a);          /* 1；a.family = IP_V6 */
a.format(buf, sizeof buf);          /* "2001:db8::1" */

Ip net = { 0 };
int prefix = 0;
Ip host = { 0 };

inet_parse_cidr(S("10.0.0.0/8"), &net, &prefix);      /* 1，prefix = 8 */
inet_parse(S("10.1.2.3"), &host);
net.cidr_contains(prefix, &host);              /* 1 */

u16 port = 0;
inet_parse_port(S("8443"), &port);                    /* 1 */
```

`Ip` 是纯值（`int family` + 16 字节），v4 用前 4 字节，所以同一个类型能装两种
地址、也能互相比较（`eq` 要求族相同）。

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
import std.encoding.bitbuf;

char buf[16] = { 0 };
BitWriter w = bw_open(buf, (int)sizeof buf);

w.put(2, 3)   catch e { return e.id; };   /* 版本 3 位 */
w.put(0x1f, 5) catch e { return e.id; };  /* 标志 5 位 */
w.put(300, 16) catch e { return e.id; };  /* 长度 16 位 */
w.put_bytes(S("hi")) catch e { return e.id; };
w.bit_count();                                   /* 40 */

BitReader r = br_open((char[:]){ buf, w.byte_count() });
u32 ver = 0;
r.get(3, &ver) catch e { return e.id; };  /* ver = 2 */
```

* **位序是 MSB-first**（同一字节里先出现的是高位）——Deflate/JPEG 之外的格式
  基本都这样；要 LSB-first 请在调用方自己翻位；
* 写侧零分配，缓冲是调用方的：`bw_open` 不碰它，第一次写某字节时才把那一格
  清 0，所以没写到的地方保持原样；
* **越界是错误而不是截断**：写侧 `FULL`、读侧 `SHORT`，两种情况都**不动游标**、
  也不写半截（一次调用要么整段成功要么整段失败）；
* `w->patch(at, v, n)` 在字节对齐处回填 8/16/32 位**且不移动游标**，
  用于「长度先留空、写完再补」；`align`/`align` 补/跳 0 到字节边界；
* `put_bytes`/`get_bytes` 要求已经在字节边界上，否则 `INVAL`（半个字节的
  读写在协议里没有意义）；
* `n == 0` 是合法空操作，`n` 超过 32/64 是 `INVAL`。

错误码记在 `w.err`/`r.err` 里（第一个错误的 id），`ok`/`r.err` 可以事后查。

### `pqueue`：二叉堆

```c
import std.container.pqueue;
import std.container.sort;                       /* cmp_int */

@PQueue@int q = new PQueue@int;
q->init(&cmp_int, 0);                  /* 0 = 小顶堆，1 = 大顶堆 */

q->push(5) catch e { return e.id; };
q->push(1) catch e { return e.id; };

int top = 0;
q->peek(&top);                         /* 1，top = 1 */
q->pop(&top);                          /* 1，top = 1 */
```

和 `std.container.vec` 同族：托管容器、只能 `new`、方法接收者是指针。比较器是 **qsort
风格** `int (*cmp)(const void *, const void *)`，和 `std.container.sort` 的 `cmp_int`/
`cmp_u32`/`cmp_str` 直接通用（`vec->sort` 也是这套）。**没设比较器时按字节序**
（`memcmp`）比，和 `sort_lex` 同口径：纯无符号小整数够用，负数会排到正数后面。

| 方法 | 说明 |
| --- | --- |
| `push(v)` | 压入，摊销 O(log n)；分配失败给 `std.core.err.NOMEM` |
| `pop(&out)` / `peek(&out)` | 弹顶/看顶；堆空返回 0（不是错误） |
| `push_pop(v, &out)` | 弹顶再压入，一次 sift，top-k 的标准写法 |
| `heapify()` | 用现有 `data[0..n)` 重建堆（整段读入之后） |
| `is_valid()` | 堆序检查，测试/断言用 |

top-k 的惯用法（留最大的 3 个）：小顶堆 + 满了就 `push_pop`——堆顶是当前
「入选里最小的」，新值比它大才替换。

### `ini`：INI 配置

```c
import std.data.ini;

char *text = "[db]\nhost = localhost\nport = 5432\n";
char[:] t = { text, (int)strlen(text) };

IniReader r = ini_open(t);
IniEntry e = { { { (char *)0, 0 } }, { { (char *)0, 0 } }, \
               { { (char *)0, 0 } }, 0 };
while (r.next(&e) == 1) {
    /* e.section / e.key / e.value 都是输入上的视图，e.line 是行号 */
}

Str host = { { (char *)0, 0 } };
ini_lookup(t, S("db"), S("host"), &host);      /* 1 */
```

和 `std.data.csv` 一个思路：**不分配、不复制**，字段都是指向输入文本的视图，所以输入
必须活到用完为止。语法有意做小：

* 行分隔 `\n`（`\r\n` 也认）；空行忽略；trim 之后以 `#` 或 `;` 开头的行是
  **整行注释**——行内的 `#` 不是注释（值里可以有 URL 片段）；
* `[section]` 切节，节名 trim，同名节等于合并；第一节之前的键属于**根区**
  （`section` 是空视图）；
* `key = value` 用**第一个** `=` 分隔，两侧 trim，所以值里可以再出现 `=`；
  值可以为空（`k =`），键不能为空；
* 其它非空行都是**语法错误**：`next` 返回 -1，行号在 `r.err`（1 起），
  之后可以继续读下一行；`ini_lookup`/`ini_has_section`/`ini_count` 遇到坏行
  直接放弃（`ini_count` 给 -1），不假装读到了后面的键；
* 键或节重复时 `ini_lookup` 给**最后一个**（「后写覆盖先写」）。

不做类型转换、不处理转义、不合并续行、不解析引号——拿到 `Str` 之后用
`std.text.str` 的 `to_int`/`trim` 等自己处理。

### `props`：`.properties` / `.env` 配置

```c
import std.data.props;

PropsReader r = props_open(S("name = demo\nexport PORT=8080\npaths = a:b\\\n        c\n"));
PropEntry e = { { { (char *)0, 0 } }, { { (char *)0, 0 } }, 0 };
while (r.next(&e) == 1) {
    char buf[64] = { 0 };
    int n = e.decode((char[:]){ buf, (int)sizeof buf });   /* 转义在这一步才解 */
}

Str v = { { (char *)0, 0 } };
props_lookup(S("PORT = 8080\n"), S("PORT"), &v);   /* 原始值视图；重复键给最后一个 */
```

* 分隔符认 `=`、`:`、空白三种（`a=1`、`a: 1`、`a 1` 都是 `a` → `1`）；键前的
  `export ` 跳过；trim 之后以 `#`/`!` 开头的行是注释；值可以为空，键不能为空。
* `key`/`value` 都是输入上的视图，**转义保持原样**：`\n` `\r` `\t` `\f` `\\`
  `\:` `\=` `\#` `\!` `\ `、`\uXXXX`（代理对合并）、行尾 `\<换行>` 续行，全在
  `props_decode` / `e.decode(buf)` 里处理。未知转义丢掉反斜杠（和 Java 一致），
  坏的 `\u` 给 -1，缓冲放不下一个字节都不写。
* 续行的 `value` 视图跨过 `\` 与换行（原始区间），解码时连同续行开头的空白一起
  去掉；所以整段输入必须活到用完。
* 语法错误（空键、`=v`）让 `next` 返回 -1，行号在 `r.err`（1 起），之后可以继续
  读下一行；`props_lookup`/`props_count` 遇到坏行给 0/-1。

### `multipart`：`multipart/form-data`

```c
import std.net.multipart;

Multipart m = mp_open(body, http_msg.header(S("Content-Type")));
/* 或者已经有 boundary：mp_open_boundary(body, S("----X")) */
Part p = { { { (char *)0, 0 } }, { { (char *)0, 0 } }, { { (char *)0, 0 } },
           { { (char *)0, 0 } }, { { (char *)0, 0 } }, { (char *)0, 0 }, 0 };
while (m.next(&p) == 1) {
    /* p.name / p.filename / p.content_type 是视图；p.body 是 char[:] */
}
m.count();          /* 不改变解析器状态 */
```

* `boundary` 从 Content-Type 的 `; boundary=...` 取（`mp_param` 认引号，引号里的
  `;` 不切）；`ok = 0` 表示没有可用 boundary，`next`/`count` 返回 -1。
* 第一个分隔符之前的 preamble、`--boundary--` 之后的 epilogue 都跳过；分隔符必须
  在 body 开头或 `\n` 之后，且后面只能跟 CRLF/LF 或 `--`——`--bX` 这种伪分隔符
  当普通 preamble 内容，不会让解析失败。
* 头块到空行为止（`\r\n\r\n` 或 `\n\n`），头字段名大小写不敏感；头块缺失时正文
  从分隔符后开始（紧邻的那个空行仍然不算正文）；正文到下一个分隔符前的 CRLF/LF
  为止。
* 零分配：不做 URL 解码、不落盘、不处理嵌套 multipart（`multipart/mixed` 的正文
  原样给出，需要时递归 `mp_open`）。`mp_param`/`mp_header` 可以单独用来解析任何
  `a; k=v; k2="v2"` 形状的头字段。

### `punycode`：Punycode 与 IDNA 的 `xn--`

```c
import std.encoding.punycode;

char out[64] = { 0 };
punycode_encode(S("münchen"), (char[:]){ out, (int)sizeof out });     /* 10, "mnchen-3ya" */
idna_encode(S("中文.cn"), (char[:]){ out, (int)sizeof out });          /* 14, "xn--fiq228c.cn" */
idna_decode(S("xn--fiq228c.cn"), (char[:]){ out, (int)sizeof out });   /* 9, "中文.cn" */
```

* 两层分开：`punycode_*` 是 RFC 3492 的 Bootstring，纯 ASCII 输入也带结尾的 `-`
  （`abc` → `abc-`，协议原文）；`idna_*` 按 `.`（也认 U+3002/U+FF0E/U+FF61）切
  标签，只把含非 ASCII 的标签编成 `xn--...`，纯 ASCII 标签原样拷贝，并检查标签
  63 字节 / 整条域名 255 字节的上限。
* `*_len` 给**精确**长度（不是上界）；写调用方缓冲，放不下返回 -1 且一个字节都
  不写。
* 不做 Unicode 归一化、不做大小写折叠（没有大小写表）：`MÜNCHEN.de` 不会得到
  `münchen.de` 的编码，要 IDNA2003 的映射请先把输入归一。
* 解码严格：基本码点必须 < 0x80、数字只认 `a-z`/`0-9`、拒绝代理区与
  > U+10FFFF 的码点，delta/w 的乘加都做溢出检查。`idna_decode` 只接受 ASCII 的
  A-label 输入。

### `width`：终端显示宽度

```c
import std.text.width;

text_width(S("中文🙂"));        /* 2 + 2 + 2 = 6 列 */
char cell[16] = { 0 };
text_pad(S("abc"), (char[:]){ cell, (int)sizeof cell }, 5);            /* 5, "abc  " */
text_clip(S("中文字符"), (char[:]){ cell, (int)sizeof cell }, 5);       /* 6, "中..." */
int bytes = text_prefix_len(S("a中b"), 2);                             /* 1, 只留 'a' */
```

* 判据是 Unicode 15.0 的 EastAsianWidth（W/F 算 2 列）与通用类别（Mn/Me/Cf 算
  0 列，另加谚文中/终声 U+1160..U+11FF）；两张有序区间表二分查找，零分配。
* Ambiguous（A）按 1 列算（非 CJK locale 的惯例）。**不做字素簇/ZWJ 序列合并**，
  逐码点相加：常见情形是对的，但 emoji 家庭这类序列在终端里可能更窄，精确排版
  别只靠它。
* `text_width` 从不失败：非法 UTF-8 字节按 1 列、控制字符 0 列；要严格判定用
  `text_width_checked`（两种情况都报 -1）。
* `text_prefix_len`/`text_fit`/`text_pad`/`text_clip` 只在码点边界切，不会留下
  半个 UTF-8 序列；`text_pad` 宽了先截、窄了补空格到恰好 N 列，`text_clip` 截断时
  补 `...`（总宽度仍 ≤ N）。都写调用方缓冲，放不下返回 -1 且不写。

### `regex`：回溯式正则

```c
import std.text.regex;

Regex re = re_compile(S("^(\\S+) (ERROR|WARN): (.*)$"));
Match m = { 0 };
if (re.find(line, &m)) {
    char[:] level = m.group(line, 2);
    char[:] msg = m.group(line, 3);
    printf("%.*s %.*s\n", level.len, level.data, msg.len, msg.data);
}
```

* 支持：字面量（UTF-8 按码点）、`.`、`[...]`/`[^...]`/`[a-z]`、`*` `+` `?`
  `{m}` `{m,}` `{m,n}`（后缀 `?` 懒惰）、`(...)` 捕获组、`(?:...)`、`|`、
  `^` `$`、`\d \D \w \W \s \S \b \B`、`\n \t \r \f \v \0 \xHH` 与标点
  转义。**不支持**反向引用、前后瞻、命名组、`[[:alpha:]]`、`\p{..}`。
* **位置是字节、匹配按码点**：`Match.start`/`end` 和 `group` 都是字节偏移，正好
  切 `char[:]`；`.` 吃一个码点。`\d \w \s \b` 按 ASCII 判定，`RE_ICASE` 只折 ASCII。
* 量词默认贪婪；`find` 从第一个能匹配的位置开始，`full` 等价于 `^(?:pat)$`
  （会为「吃到文本结尾」回溯），`find_all` 遇到空匹配也会前进一个码点。
* 容量上限写在枚举里：64 个节点、8 个字符类（每类 16 段区间）、9 个捕获组、
  `{m,n}` 的 n ≤ 255；匹配深度 4096，超了 `Match.aborted` 置 1、`find` 返回 0。
* 单字符量词（`.*`、`[a-z]+`、`\d{2,4}`）和「定长链 + 一层捕获组」的组量词
  （`(ab)*`）走迭代快路径，是线性的；分支/嵌套组上的量词才是经典回溯。

### `diff`：行级差异与 unified 输出

```c
import std.core.mem;
import std.text.diff;

char work[16384] = { 0 };
Arena ar = arena_make(work, (int)sizeof work);
Diff d = diff_make(&ar, old_text, new_text);
char out[1024] = { 0 };
int n = d.write_unified((char[:]){ out, (int)sizeof out }, 3);   // ctx=3
```

* Hirschberg LCS：时间 O(na*nb)、额外空间 O(nb)（两行 DP + 匹配表），先剥公共
  前后缀，所以「长文件里改几行」接近线性。工作区全部从 `Arena` 分，不 malloc。
* 行切分：`\n` 结尾算一行，最后一行没有 `\n` 也算一行，空文本 0 行；`\r` 算行内容。
* 输出：`write_script`（` `/`-`/`+` 前缀）与 `write_unified`（`@@ -a,b +c,d @@`，
  不含 `---`/`+++`，相邻 hunk 按 `ctx` 合并）；`script_len`/`unified_len` 给精确
  长度，都是先量再写、放不下返回 -1 且不写半截；`op_at` 逐条取编辑动作。
* 工作区不够时 `err = DIFF_ENOMEM`，之后所有写接口返回 -1。

### `fuzzy`：编辑距离、相似度与子序列

```c
import std.text.fuzzy;

lev_distance(S("kitten"), S("sitting"));        /* 3 */
lev_ratio(S("install"), S("instal"));           /* 857（千分比） */
damerau_distance(S("ab"), S("ba"));             /* 1（相邻换位） */
fuzzy_best(S("instal"), cands, n, 2);           /* 最近的候选下标 */
```

* 全部**按码点**（`中文` 对 `中问` 是 1，不是 3），坏 UTF-8 字节算一个字符。
* `lev_distance` 是 Levenshtein；`damerau_distance` 是 OSA（相邻换位算 1，不做
  完整 Damerau）；`lev_ratio` = `(maxlen - dist) * 1000 / maxlen`。
* `lev_distance_max` 只看「超过阈值没有」：长度差先挡一道，超过给 `maxd+1`。
* `subsequence_match`/`subsequence_score` 是 fzf 式判据与打分（ASCII 大小写不敏感，
  连续命中 +4、词首命中 +6、跳过字符扣分）；`fuzzy_best` 用它做「did you mean」。
* `FUZZY_MAX` = 256 码点：超了返回 -1（栈上两张码点表 + 两三行 DP，约 5KB）。

### `wrap`：按显示列折行、缩进与居中

```c
import std.text.wrap;

Wrap w = wrap_indented(40, 2, 6);      /* 40 列，首行缩进 2、续行 6 */
char out[512] = { 0 };
int n = w.write(text, (char[:]){ out, (int)sizeof out });
```

* 宽度是**显示列**（`std.text.width` 的 `cp_width`）：CJK/全角 2 列、组合符 0 列。
* 空白分隔词（同一行压成一个空格）；**CJK 字符之间可以直接断行**；超长词默认按
  码点硬切（`WRAP_BREAK_LONG`），清掉这个开关就整段溢出、下一段再换行。
* 输入空行原样保留，`\r\n` 按 `\n` 处理；输出每行都以 `\n` 结尾，空输入 0 字节。
* `count`/`lines` 与 `write` 共用一套渲染，长度精确；`write` 放不下返回 -1 且不写
  半截。`text_indent`/`text_center` 是整段缩进与居中（各有 `*_len` 版本）。

### 嵌入式基础：`crc` / `fixed` / `pool` / `frame`

四个模块都只依赖 `std.core.types`，不分配、不链 libm、不碰 `pthread`，所以
freestanding / 裸机程序可以直接用（见 [manual/freestanding.md](manual/freestanding.md)）。
它们对应无 FPU、无堆、只有一条 UART 的板子上最常见的四件事：校验、定点、固定块
内存、串行分帧。

```c
import std.core.crc;
import std.core.fixed;
import std.core.pool;
import std.encoding.frame;

struct Sample { int id; Fix temp; };

u8 req[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0a };
u16 c = crc16_modbus(req, 6);               /* 0xcdc5：线上低字节先发 */
u8 rom[8] = { 0x28, 0xff, 0x64, 0x1e, 0x5d, 0x16, 0x04, 0x27 };
u8 romcrc = crc8_maxim(rom, 7);             /* 0x27：1-Wire ROM 的 CRC */
u16 f = fletcher16("abcde", 5);             /* 0xc8f0 */
u8 l = lrc8(req, 6);                        /* 0xf2：Modbus ASCII 的尾字节 */

Fix t = fix_from_frac(235, 10);             /* 23.5 */
Fix doubled = t.mul(fix_from_int(2));       /* 47.0 */
int rounded = t.round();                    /* 24 */
Fix two = fix_from_int(2);
Fix root = two.sqrt();                      /* 1.4142… */
Fix half = fix_from_frac(1, 2);
Fix s = half.sin();                         /* 0.4794… */
Q15 q = q15_from_f64(0.5);
Q15 sq = q.mul(q);                          /* 0.25 */

u8 store[256] = { 0 };
Pool p = pool_make(store, (int)sizeof store, (int)sizeof(struct Sample));
struct Sample *s0 = p.alloc();              /* 块里直接放结构体 */
p.free(s0);                                 /* O(1)，按 LIFO 复用 */
p.reset();                                  /* 整池收回 */

u8 payload[7] = { 0x11, 0x00, 0xc0, 0xdb, 0x41, 0x00, 0x7f };
u8 wire[32] = { 0 };
u8 back[16] = { 0 };
int n = cobs_encode_frame(payload, 7, wire, (int)sizeof wire);   /* 结尾补 0 */
int m = cobs_decode_frame(wire, n, back, (int)sizeof back);
n = slip_encode_frame(payload, 7, wire, (int)sizeof wire);       /* END…END */
m = slip_decode_frame(wire, n, back, (int)sizeof back);
```

* `crc`：CRC 目录里的 check 值（`"123456789"`）钉在 `test` 块里；`poly` 一律写
  正规形式，`refin` 决定内部是否反射，init/xorout 由具名包装加。CRC-32/IEEE
  仍在 `std.core.hash` 的 `crc32_bytes`，这里补的是嵌入式链路上的短校验。逐位
  算、不建 256 项表——小 MCU 上省 512 字节 `.rodata` 比省这点 CPU 值钱。
* `fixed`：Q16.16 的 `raw` 字段可以直接读写/比较；乘除法的中间结果走 `i64`，
  四舍五入（.5 远离 0），溢出**饱和**；`div` 除数为 0、`sqrt` 负数都给 0。
  `sin`/`cos` 是 Taylor 到 x^9（先规约到 [-π/2, π/2]），误差 < 1e-5。Q15 的
  1.0 = 32767、-1.0 = -32768，`mul`/`add_sat`/`sub_sat` 饱和。
* `pool`：块大小和基址向上对齐到 16 字节（32 位上 8 字节），但块起点不保证是
  `block_size()` 的整数倍；`free` 只做范围和步长检查，**不检测重复释放**（那要
  O(n) 扫链表）；坏参数得到容量 0 的池。
* `frame`：`cobs_max_len(n)` 是上界、`cobs_encoded_len(src, n)` 是精确长度；
  COBS 的 0 只能当分隔符（解码时负载里出现 0 就报错）。SLIP 按 RFC 1055 转义
  `0xC0`/`0xDB`，`slip_decode_frame` 允许首尾各省略一个 END。两侧都是「先量
  再写」，缓冲不够返回 -1 且一个字节都不写；解码先扫一遍校验格式，失败时目标
  缓冲不变。

### `bignum`：任意精度整数

```c
import std.core.bignum;

Big a = big_parse(S("123456789012345678901234567890"));
Big b = big_from_i64(7);
Big q = { 0 }, r = { 0 };
if (a.divmod(b, &q, &r)) {          /* q*7 + r == a */
    char out[64] = { 0 };
    q.format((char[:]){ out, (int)sizeof out });   /* 十进制 */
}
```

* 表示是「符号 + 32 个 32 位 limb」（小端），容量 **1024 位**（十进制约 308 位）。
  够 RSA-1024、阶乘、大数进制转换；要更大就改 `BIG_LIMBS` 重新编译（值类型，没有堆）。
* **截断除法**：`divmod` 的商向零取整、余数同被除数符号（和 C 的 `/`、`%` 一致），
  除数为 0 返回 0；`div`/`mod` 是它的两个便捷入口。
* 超出 1024 位（加法最高进位、乘法结果、幂、解析的数字）一律 `err = 1`，并随结果
  传播；`cmp` 只看值，`is_err` 要单独查。
* 进制 2..36，输出小写、输入大小写都收；`format`/`format_base` 先写临时缓冲，
  放不下返回 -1 且不写半截。

### `decimal`：十进制定点

```c
import std.core.decimal;

Dec price = dec_parse(S("19.99"));
Dec sub = price.mul(dec_from_i64(3));          /* 59.97 */
Dec tax = sub.mul(dec_parse(S("0.065")));      /* 3.89805 */
tax = tax.round(2, DEC_HALF_UP);               /* 3.90 */
```

* 一个 i64，单位 1e-9：范围约 ±9.22e9、9 位小数。金额、单价、税额、汇率用它对，
  不要拿二进制浮点凑（`0.1 + 0.2` 就是精确的 `0.3`）。
* **乘除先算 128 位中间值再除 1e9**，最后按 half-up 收尾——tcc 没有 `__int128`，
  模块里自带 `u128_mul`/`u128_div`（两个 64 位半宽拆开做），所以没有二进制误差。
* `round(places, mode)` 是十进制舍位（0..9 位），`mode` 是 `DEC_TRUNC`/`DEC_HALF_UP`/
  `DEC_HALF_EVEN`/`DEC_FLOOR`/`DEC_CEIL`；`to_i64` 用同一套模式取整。
* `parse` 收 `[+-]digits[.digits]`，小数超过 9 位报错（不悄悄截断）；`format`
  输出恰好 N 位小数；溢出/除零/解析失败都给 `err`。`to_f64`/`dec_from_f64` 会有
  二进制误差，只在对外交换时用。

### `rational`：精确分数

```c
import std.core.rational;

Rat h = rat_zero();
for (k = 1; k <= 10; k++)
    h = h.add(rat_make(1, k));       /* 7381/2520 */
```

* 值总是最简形式（`den > 0`、`gcd == 1`、零是 `0/1`），所以相等的有理数表示唯一。
* 加减乘除都先约分再乘，尽量把溢出推后；**加减的中间分子**放不进 i64 时报 `err`
  （即使约分后结果放得下）——没有 128 位整数的代价，极端大数请用 bignum/decimal。
* `cmp` 用连分数比较：交叉相乘会溢出的两个数（`MAX/2` 对 `MAX/1`）也能比出正确
  结果，不经过 double。`to_i64` 向零截断，`to_f64` 是近似。

### `complex`：复数

```c
import std.core.complex;

Complex z = cplx(3.0, 4.0);
f64 m = z.abs();                     /* 5 */
Complex w = z.mul(cplx(1.0, -2.0));  /* 11 - 2i */
```

* 只做四则 + 模 + 共轭 + 归一化 + 整数幂；`abs` 用 `std.core.math` 里自实现的
  `sqrt`，不链 libm。
* 除零按 IEEE 给 `inf`/`nan`（和浮点一致），要判定用 `is_finite()`；`eq` 是逐位
  精确比较，浮点运算之后请用 `near(o, eps)`。
* **没有** `arg`/`exp`/`log`/`sin`：这些要 `atan2`/`exp`，标准库目前不提供。

### `lz4`：LZ4 块压缩/解压

```c
import std.encoding.lz4;

int table[lz4_table_size()];          /* 1024 个 int，约 4KB，调用方给 */
char packed[4096] = { 0 };
int n = lz4_compress(src, src_len, packed, (int)sizeof packed, table,
                     lz4_table_size());
char back[4096] = { 0 };
int m = lz4_decompress(packed, n, back, (int)sizeof back);
```

* 实现的是 **LZ4 块格式**（不是带帧头/校验和的 frame 格式），和上游
  `LZ4_compress_default` 兼容：test 块里有手写的标准块向量，也和独立的参考实现
  做了双向交叉验证。
* 零分配：哈希表是调用方的 `int[lz4_table_size()]`，输出写调用方缓冲；压缩前按
  `lz4_compress_bound(n)` 检查容量，放不下返回 -1 且一个字节都不写。
* 压缩器遵守 LZ4 的两条格式承诺：最后 5 字节一定是字面量、最后一个匹配不早于
  结尾前 12 字节。
* 解码是流式校验：偏移为 0 / 越过已产出的数据、扩展长度被截断、输出放不下都返回
  -1；失败时目标缓冲内容未定义（已写了一部分），要保留原内容请先解到临时缓冲。
* 空输入的编码是 1 个字节（`0x00`），解码回 0 字节。

### `rle`：PackBits 游程与一阶差分

```c
import std.encoding.rle;

char packed[256] = { 0 };
int n = rle_encode(src, src_len, packed, (int)sizeof packed);
char diffs[256] = { 0 };
delta_encode(samples, n, diffs, (int)sizeof diffs);
```

* PackBits（Apple/TIFF）：控制字节 `0..127` 表示后面跟 `c+1` 个字面量，`129..255`
  表示后面那 1 个字节重复 `257-c` 次，`128` 是空操作；单段最多 128 字节。
* `delta_encode`/`delta_decode` 是一阶差分（模 256）：缓变的 ADC/计数器差分后
  全是小值，再交给 RLE 或 LZ4 能压得更狠；两者严格互逆。
* `rle_bound` 是上界、`rle_encoded_len` 是精确长度，先量再写；解码先校验截断，
  坏数据/放不下返回 -1。

### `base85`：Ascii85 与 Z85

```c
import std.encoding.base85;

char buf[64] = { 0 };
int n = a85_encode(S("hello"), buf, (int)sizeof buf);   /* "BOu!rDZ" */
a85_decode((char[:]){ buf, n }, out, (int)sizeof out);
```

* 两种都是 4 字节 → 5 个字符（85^5 > 2^32）。
* **Ascii85**（Adobe）：字母表 `!`..`u`；4 个零字节写成 `z`；结尾不足 4 字节输出
  n+1 个字符；解码跳过空白、遇到 `~>` 停止（`<~` 开头由调用方剥掉）。部分组补齐
  用 `u`，数值超过 32 位视为非法。
* **Z85**（ZeroMQ）：字母表 `0-9a-zA-Z.-:+=^!/*?&<>()[]{}@%$#`；编码长度必须是
  4 的倍数、解码长度必须是 5 的倍数，没有 `z` 简写、不跳空白。
* 都写调用方缓冲，放不下返回 -1 且不写半截。

### `hexdump`：十六进制文本化与反向解析

```c
import std.encoding.hexdump;

HexOpts o = hex_opts();          /* 16 字节一行、小写、带 ASCII 列、偏移 0 */
char text[256] = { 0 };
int n = hexdump(src, src_len, (char[:]){ text, (int)sizeof text }, o);
hexundump((char[:]){ text, n }, back, (int)sizeof back);
```

* 固定布局：8 位偏移（`base + 行号 * width`，32 位取模）+ 2 空格 + 每字节 3 字符
  （第 8 字节后多一个分组空格）+ ` |ASCII 列|`；不足一行补空格，不可打印字节写 `.`。
* `width` 允许 1..64（0 表示默认 16）；`upper` 大写十六进制；`ascii = 0` 去掉
  ASCII 列；`base` 是首行偏移。
* `hexundump` 只认数据：行首恰好 8 个十六进制数字 **且后面跟空白** 才当偏移丢掉
  （所以 `DeAdBeEf` 这种纯数据行不会被误吞），遇到 `|` 或行尾就丢掉本行剩余部分。
* `hexdump` 先按 `hexdump_len` 算长度，放不下返回 -1 且不写半截。

### `toml`：TOML 读取器

```c
import std.data.toml;

Str doc = { S("title = \"demo\"\n[server]\nhost = \"localhost\"\nport = 8080\n") };
TomlReader r = toml_open(doc);
TomlEntry e = { 0 };
while (r.next(&e) == 1) {
    if (e.kind == TOML_KEY && e.section.s.len == 6) {
        char host[32] = { 0 };
        if (e.key.s.len == 4 && memcmp(e.key.s.data, "host", 4) == 0)
            e.decode((char[:]){ host, (int)sizeof host });
    }
}
```

* 事件式、零分配：`e.section`/`e.key`/`e.raw` 都是输入上的视图；`e.decode` 才把
  基本字符串的转义（`\n`、`\uXXXX`…）写进调用方缓冲，字面量字符串（`'...'`）
  不转义。
* 数字/布尔/数组各走各的：`toml_int`（十进制、`0x`/`0o`/`0b`、`_`、`+/-`，溢出
  返回 0）、`toml_float`（去掉 `_` 交给 `std.text.num` 的 `parse_f64`，`inf`/`nan`
  自己处理）、`toml_bool`（只认小写）、`toml_array_len`/`toml_array_at`（数组可
  跨行、元素保留原文）。
* 表 `[a.b]` 与表数组 `[[a.b]]` 都会更新 `section`；**不支持**多行字符串、内联表
  的类型化、点号键和日期时间语义（后两者按原文保留）。
* 语法错误（缺 `=`、表头/字符串没闭合、空键/空值）`next` 返回 -1，行号进 `r.err`；
  坏行会被跳过，所以错误之后还能继续读。

### `xml`：XML 扫描器

```c
import std.data.xml;

XmlReader r = xml_open(S("<doc><a x=\"1\">hi</a><b/></doc>"));
XmlEvent e = { 0 };
while (r.next(&e) == 1) {
    if (e.kind == XML_START) {
        Str v = { { (char *)0, 0 } };
        if (e.attr(S("x"), &v)) { /* v 是引号里的原文 */ }
    }
}
```

* 四种事件：开始标签、结束标签、自闭合标签、文本；注释/处理指令/DOCTYPE 跳过，
  CDATA 当作文本吐出。
* 属性在事件上按需解析：`attr_count`、`attr_at(i, &attr)`、`attr(name, &value)`；
  属性值保留原文（`&amp;` 不在这里解），要解用 `xml_unescape`（5 个预定义实体 +
  `&#NNN;`/`&#xHH;`，坏引用返回 -1）。
* **做**标签配对校验：32 层的名字栈，`</a>` 必须和最近的开始标签同名；读完还有
  未闭合元素也算错。出错时 `next` 返回 -1，`r.err` 是出错字节偏移 + 1。
* 不做命名空间、DTD/自定义实体、编码转换；元素名按 UTF-8 字节扫描（宽松）。

### `query`：查询串 / 表单编码

```c
import std.data.query;

QueryReader r = query_open(S("q=hello+world&page=2"));
QueryPair p = { 0 };
while (r.next(&p) == 1) { /* p.name / p.value 是原文视图 */ }

char buf[32] = { 0 };
query_get(S("q=hello+world"), S("q"), (char[:]){ buf, (int)sizeof buf });  /* "hello world" */
```

* 语义是 `application/x-www-form-urlencoded`：参数之间 `&`、只按**第一个** `=`
  切、空段忽略、没有 `=` 的段 `has_eq = 0`。`+` 表示空格，和 `std.net.url` 的
  百分号编解码**不同**，所以单独一个模块。
* 解析零分配（`name`/`value` 保留 `%XX` 与 `+`）；`query_decode` 解码（坏转义
  返回 -1），`query_encode` 编码（空格写 `+`、其余按 unreserved 原样、其它写大写
  `%XX`）。
* `query_get`/`query_get_nth` 先解码名字再比较，值解码后写调用方缓冲；名字最长
  255 字节（内部临时缓冲），超了按未找到处理。
* `query_build` 按 `a=b&c=d` 构建，先量再写，放不下不写半截。

### `template`：`{{name}}` 模板

```c
import std.text.template;

Var v[2] = { { S("name"), S("World") }, { S("n"), S("3") } };
Template t = tpl(S("Hello {{name}}, {{ n }} messages"));
char out[128] = { 0 };
int n = t.render(v, 2, (char[:]){ out, (int)sizeof out });
```

* `{{name}}` 两侧空白忽略；`{{{{` 输出字面量 `{{`，单独的 `}}` 原样输出；值里的
  内容不递归展开、不做 HTML 转义。
* 缺变量：`render`/`render_len` 严格返回 -1；`render_opt(..., strict = 0)` 用空串
  代替。`{{` 没闭合或名字为空都返回 -1。
* `vars(out, cap)` 去重收集模板里出现过的名字（按出现顺序，不依赖额外存储）；
  返回总数，`cap = 0` 可用来量个数。
* 先量再写，放不下返回 -1 且不写半截。

### `args`：命令行选项解析

```c
import std.os.args;

ArgSpec specs[2] = { { { S("--verbose") }, 'v', 0 },
                     { { S("--port") },    'p', 1 } };
Args a = args_open(argc, argv);
Arg arg = { 0 };
while (a.next(specs, 2, &arg) == 1) {
    if (arg.kind == ARG_VALUE)   /* arg.value（Str）是值 */
        ;
    else if (arg.kind == ARG_FLAG)
        ;
    else                         /* ARG_POSITIONAL：arg.name 是参数本身 */
        ;
}
```

* 为什么要有规格表：`--port 8080` 和 `--flag file` 字面上分不清谁吃值，所以
  先声明 `has_value`，解析器按声明消费。
* `--name=value`、`--name value`、`-n value`、`-n=value` 都认；`-abc` 合并写法
  **不支持**（报 `ARG_EUNKNOWN`）；`--` 之后全部按位置参数；单个 `-` 也是位置参数。
* `arg.name` 是实际写的形式（`-p` 或 `--port`），`arg.spec` 是规格表下标（位置
  参数是 -1），`arg.index` 是 argv 下标。
* 选项与位置参数可以任意穿插（GNU 风格）；错误码 `ARG_EUNKNOWN`/`ARG_EMISSING`
  在 `a.err` 里，游标停在出错位置。

### `table`：对齐表格

```c
import std.text.table;

char[:] head[2] = { S("name"), S("qty") };
char[:] cells[4] = { S("a"), S("12"), S("bb"), S("3") };
int align[2] = { TBL_LEFT, TBL_RIGHT };
Table t = table_make(2);
char out[256] = { 0 };
t.render(head, cells, 2, align, (char[:]){ out, (int)sizeof out });
```

```
+------+-----+
| name | qty |
+------+-----+
| a    |  12 |
| bb   |   3 |
+------+-----+
```

* 列宽按**显示列数**（`std.text.width`：CJK 2 列、组合符 0 列），中英混排能对齐。
* `Table` 字段：`border`（外框与横线，默认 1）、`sep`（列间 `|`，默认 1）、
  `pad`（格子左右空格数，默认 1；只在有边框时生效）。
* `headers` 可为 0（无表头）；`cells` 是 `nrows*ncols` 个切片（行优先）；
  `aligns` 可为 0（全左对齐）或 ncols 个 `TBL_LEFT`/`TBL_RIGHT`/`TBL_CENTER`。
* `table_len` 与 `render` 同一套逻辑，先量再写；放不下返回 -1 且不写半截。

### `natural`：自然排序与人类可读化

```c
import std.text.natural;

natural_cmp(S("file2"), S("file10"));      /* -1：数字段按整数比 */
humanize_bytes(1536, out);                 /* "1.5 KiB" */
humanize_duration(3723000, out);           /* "1h 2m 3s" */
humanize_int(-1234567, out);               /* "-1,234,567" */
```

* `natural_cmp` 把连续数字当成整数比较（前导 0 不改变大小），数字段相等就继续
  比剩余部分；`natural_cmp_ci` 是 ASCII 大小写不敏感版。
* 字节数用 1024 进制、单位 `B/KiB/MiB/GiB/TiB`，非 B 带一位小数（四舍五入）；
  `parse_bytes` 反向解析（`1.5 KiB`、`2MiB`、`512`、`1k`，大小写不敏感）。
* 时长收毫秒，输出 `1h 2m 3s` / `850ms` / `0ms` / `-1s`；`parse_duration` 收
  `1h30m`、`500ms`、`2s`、`1h 2m 3s`，**裸数字按秒**。
* 所有格式化函数都写调用方缓冲、放不下返回 -1 且不写半截。

### `proc`：子进程

```c
import std.os.proc;

char *argv[3] = { (char *)"sh", (char *)"-c", (char *)"exit 7" };
ProcStatus st = { 0 };
if (proc_run(3, argv, &st) && st.exited)
    printf("%d\n", st.exit_code);         /* 7 */
```

* `proc_run`/`proc_run_sh` 返回 1 表示子进程被完整 wait 到（不管退出码多少）；
  fork/wait 失败返回 0。退出状态在 `ProcStatus`：`exited`+`exit_code` 或
  `signaled`+`signal`。
* 命令找不到时子进程 `_exit(127)`，所以看到的是 `exited` 且 `exit_code == 127`。
* `proc_spawn` + `proc_wait` 是「不等待」的拆开版；`proc_which` 在 `PATH` 里找
  可执行文件（名字带 `/` 就直接检查），找到把路径写调用方缓冲。
* 不做重定向/管道/超时/Windows；`proc_run_sh` 的命令串上限 4KB（栈缓冲，不分配）。

### `sha1`：SHA-1 与 HMAC-SHA1

```c
import std.encoding.sha1;

Sha1 s = sha1_new();
s.update("abc", 3);
u8 d[20] = { 0 };
s.final(d);                          /* a9993e36... */
char hex[41] = { 0 };
sha1_hex("abc", 3, (char[:]){ hex, (int)sizeof hex });
```

* 流式接口 `sha1_new`/`init`/`update`/`final`（`final` 之后要重用就 `init`）；
  一次性 `sha1`/`sha1_hex`；HMAC 的 `hmac_sha1`/`hmac_sha1_hex`（长 key 先摘要）；
  `sha1_eq` 是常数时间比较（20 字节）。
* SHA-1 在密码学上已经不安全（碰撞可构造），这里是为了**协议互操作**：
  WebSocket 握手、git 对象名、HTTP Digest 等。要抗碰撞用 `std.encoding.sha256`。
* 零分配、不链 libc 加密库；hex 是小写 40 字符。

### `ws`：WebSocket 帧与握手

```c
import std.net.ws;

char accept[28] = { 0 };
ws_accept(S("dGhlIHNhbXBsZSBub25jZQ=="), (char[:]){ accept, (int)sizeof accept });
/* "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" */

char frame[64] = { 0 };
int n = ws_encode(WS_TEXT, 1, "Hello", 5, 0x37fa213du,
                  (char[:]){ frame, (int)sizeof frame });
WsFrame f = { 0 };
char payload[64] = { 0 };
int m = ws_decode(frame, n, &f, (char[:]){ payload, (int)sizeof payload });
```

* 握手：`ws_accept` = `base64(SHA1(key + GUID))`，依赖 sha1 与 base64。
* 帧：`ws_encode`（FIN/opcode、7/16/64 位长度、`mask` 非 0 时掩码）/
  `ws_parse`（只解析头：1 完整、0 还不够、-1 坏帧）/`ws_decode`（整帧并去掩码）。
* `mask` 是 u32，编码按大端写 4 字节掩码键、解码按同样顺序还原；掩码键要随机
  （可以用 `std.core.rand`）。
* 不做分片重组：FIN=0 的帧也按单帧解出来，`fin` 原样给出，分片由调用方自己拼。

### `dns`：DNS 报文

```c
import std.net.dns;

char q[128] = { 0 };
int n = dns_build_query("example.com", DNS_A, 0x1234,
                        (char[:]){ q, (int)sizeof q });

DnsHeader h = { 0 };
dns_header(resp, rn, &h);
char name[128] = { 0 };
DnsRR rr = { 0 };
int next = 0;
dns_rr(resp, rn, off, &rr, (char[:]){ name, (int)sizeof name }, &next);
u32 ip = 0;
dns_a(resp, rn, &rr, &ip);
```

* 解析：`dns_header` 拆 12 字节头；`dns_question`/`dns_rr` 按偏移读一条记录，
  名字支持**压缩指针**（只允许向前指、最多 64 跳防环），解码成点分字符串；
  `*next` 是下一条的偏移，顺着 ancount 遍历即可。
* 记录取值：`dns_a`（4 字节）、`dns_aaaa`（16 字节）、`dns_cname`（CNAME/NS/PTR
  的目标名字）。
* 构建：`dns_build_query` 生成 RD=1、一个问题、class IN 的标准查询。
* 不做 EDNS0、DoT、DNS over TCP 的两字节长度前缀、名字里的转义。

### `tcp`：TCP 套接字

```c
import std.net.tcp;

Tcp srv = tcp_listen("127.0.0.1", 0, 4);      /* 0 = 内核挑端口 */
int port = srv.port();
Tcp cli = tcp_connect("127.0.0.1", port, 1000);
Tcp peer = srv.accept_client();
peer.write_all("hi", 2);
char buf[8] = { 0 };
cli.read_all(buf, 2);
cli.close_socket();
```

* 工厂 `tcp_listen`/`tcp_connect`/`accept_client` 返回 `Tcp`，失败时 `fd = -1`，
  用 `ok()` 判断；其它方法对无效 fd 返回 -1/0，不会崩。
* `tcp_connect` 的 `timeout_ms >= 0` 用非阻塞 connect + `poll` 实现超时；
  `< 0` 是普通阻塞 connect。
* `read_bytes` 返回 0 表示对端关闭（EOF）、-1 表示错误/超时；`write_bytes` 可能
  只发一部分，要发完整用 `write_all`；`read_all` 读满 n 字节。
* `set_timeout`（SO_RCVTIMEO/SO_SNDTIMEO）、`set_nodelay`、`shutdown_write`（半关闭）、
  `close_socket`（重复调用安全）。
* 名字避开 libc 的 `send`/`recv`/`close`/`accept`（同名会与系统头冲突）。
* 不做 TLS；DNS 报文用 `std.net.dns`，WebSocket 分帧用 `std.net.ws`。

### `graph`：邻接表图算法

```c
import std.container.graph;

int head[5] = { 0 }, next[8] = { 0 }, to[8] = { 0 }, w[8] = { 0 };
Graph g = { 0 };
graph_init(&g, 5, head, next, to, w, 8);
g.add_edge(0, 1, 4);

int dist[5] = { 0 }, prev[5] = { 0 }, heap[5] = { 0 }, pos[5] = { 0 };
g.dijkstra(0, dist, heap, pos, prev);
```

* 表示是**前向星**：每个顶点一条边的链表（`head[u]` → `next[e]`），`add_edge`
  是 O(1)，遍历邻居顺着链表走。所有数组都由调用方给。
* `bfs`：无权最短路（每条边算 1），不可达是 `GRAPH_INF`；`dfs`：迭代前序
  （显式栈，避免深图递归爆栈）。
* `toposort`：Kahn 算法，有环返回 0；`components`：并查集算弱连通分量。
* `dijkstra`：非负权最短路 + `prev` 回溯链（负权返回 -1）；堆和 `pos` 是
  调用方给的 n 个 int 工作区。
* 队列/栈/工作区放不下时返回 -1。

### `lru`：LRU 缓存

```c
import std.container.lru;

u64 keys[4]; int vals[4], prev[4], next[4], hnext[4], ht[8];
Lru l = { 0 };
lru_init(&l, 4, 8, keys, vals, prev, next, hnext, ht);
l.put(42, 7);
int v = 0;
l.get(42, &v);          /* 命中，顺手挪到最近使用 */
```

* 哈希表（链式）+ 双向链表：`get`/`put`/`remove` 都是 O(1)；`put` 满了淘汰
  最旧的（`evicts` 加一）。
* `peek` 只看不动顺序、也不计命中；`get` 未命中 `misses` 加一。
* `lru_init` 要求 `cap >= 1`、`htsize` 是 2 的幂且 `htsize >= 2*cap`。
* 统计字段 `hits`/`misses`/`evicts` 直接读；键是 u64、值是 int。

### `trie`：字节前缀树

```c
import std.container.trie;

TrieNode pool[64] = { 0 };
Trie t = { 0 };
trie_init(&t, pool, 64);
t.put("git", 3, 1);
t.put("git add", 7, 2);
t.has_prefix("git ", 4);        /* 1 */
```

* 节点是「边上的字节 + 子节点链（first/next 兄弟）」，键可以是任意字节序列
  （含 `\0`，长度显式传），所以也能当二进制前缀索引用。
* `put`：新键 1、覆盖 0、节点池满 -1（不写坏已有结构）；`remove` 只清词尾标记，
  不回收共享节点；`has_prefix` 判「有没有以它开头的键」。
* `size` 是不同键数、`node_count` 是已用节点数。

### `region`：区间集合

```c
import std.container.region;

i64 lo[8] = { 0 }, hi[8] = { 0 };
Region r = { 0 };
region_init(&r, lo, hi, 8);
r.add(0, 10);
r.add(10, 20);                  /* 相邻，合并成 [0, 20) */
r.contains(19);                 /* 1；contains(20) 是 0 */
```

* 按 `lo` 升序保存**互不重叠**的半开区间；`add` 会合并重叠与相邻的段，失败
  （放不下/`hi <= lo`）不改动已有内容。
* `contains` 二分查找；`overlaps` 只看真正相交（相邻不算）。
* `find_gap(from, len)` 找第一个 `[g, g+len)` 完全落在空隙里且 `g >= from` 的
  位置（带 `from + len` 溢出保护）。
* 坐标是 i64、允许负数；`total` 是所有段长度之和。

### `atomic`：原子操作与自旋锁

```c
import std.core.atomic;

Atomic hits = { 0 };
hits.inc();
i64 n = hits.load();
i64 old = hits.exchange(0);
i64 want = 0;
hits.cas(&want, 1);              /* 等于 want 才换 */

SpinLock gate = { 0 };
gate.lock();
defer gate.unlock();
```

* `Atomic`（i64）与 `Atomic32`（int）方法相同：`load`/`load_relaxed`、`store`/`store_release`、
  `add`/`sub`（返回**新值**）、`fetch_add`/`fetch_sub`（返回**旧值**）、`exchange`（旧值）、
  `cas(expected*, desired)`（相等才换，1/0，失败时把当前值回写到 `*expected`）、
  `and_with`/`or_with`/`xor_with`、`inc`/`dec`。
* 默认内存序是 seq_cst；`fence()`/`fence_acquire()`/`fence_release()` 是显式屏障
  （保守实现成全屏障，tcc 还没有 `__atomic_thread_fence`，库用一个 seq_cst 的 RMW 表达）。
* 字段 `v` 是普通整数，只通过方法读写（`{ 0 }` 清零）；直接赋值不参与原子序。
* `SpinLock` 是 TAS 自旋锁：只适合很短、不会阻塞的临界区；长临界区用
  `std.os.sync` 的 `Mutex`（会让出 CPU）。
* 走编译器内建，不依赖 pthread；32 位目标上的 64 位原子可能落到 libatomic，
  这类目标用 `Atomic32`。

### `sync`：互斥锁、条件变量与信号量

```c
import std.os.sync;

Mutex m = { 0 };
m.lock();
defer m.unlock();

Cond cv = { 0 };
m.lock();
while (!ready)                   /* 被唤醒不等于条件成立 */
    cv.wait(&m);
m.unlock();
```

* `Mutex`：`init`/`destroy`、`lock`/`try_lock`/`unlock`。`{ 0 }` 在 Linux 上就是
  `PTHREAD_MUTEX_INITIALIZER`，`init()` 是显式（可移植）写法。
* `Cond`：`wait(&m)`（进来时 `m` 必须已加锁，返回时重新持有）、`wait_ms(&m, ms)`
  （1 等到 / 0 超时 / -1 出错）、`signal`/`broadcast`。
* `RWLock`：`rdlock`/`try_rdlock`/`wrlock`/`try_wrlock`/`unlock`（读锁写锁都用 `unlock`）。
* `Once`：`init()` + `call(fn)`：只有第一个线程跑 `fn`，其余等它跑完；
  `pthread_once` 不能重置，`init` 只能在 `call` 之前用一次。
* `Semaphore`：`init(v)`、`wait`/`try_wait`/`wait_ms`、`post`、`value`。
* `Barrier`：`init(n)` + `wait()`：返回 1 的那个线程是「串行的那个」，其余 0。
* 会失败的操作返回 1/0；老系统链接要加 `-lpthread`，无 libc 的 freestanding
  目标不能用这个模块。

### `thread`：线程身份与线程池

```c
import std.os.thread;

printf("cpu=%d\n", cpu_count());
thread_set_name("worker");

ThreadJob jobs[16] = { 0 };
ThreadPool pool = { 0 };
pool.start(4, jobs, 16);         /* 4 个 worker、队列容量 16 */
pool.submit(&work, &data);       /* 队列满了会等 */
pool.wait();                     /* 等队列清空、worker 全闲 */
pool.stop();
```

* `thread_self()` 是当前线程的 id（`pthread_t` 转 u64，只用于比较/日志）、
  `thread_equal(a, b)`、`cpu_count()`（取不到给 1）、`thread_yield()`、
  `thread_set_name(name)`（Linux 上最多 15 字节，只对当前线程生效）。
* `ThreadPool`：`start(workers, jobs, cap)`（`workers` 上限 `POOL_MAX`=32，任务队列
  由调用方给）、`submit`（满时阻塞，背压）/`try_submit`（满返回 0）、
  `wait()`（只保证「现在闲」，之后还能继续投）、`stop()`（不再接任务并 join
  全部 worker，重复调用安全）、`pending`/`running`/`done`/`workers`/`is_started`。
* 任务函数是 `void (*fn)(void *)`，`arg` 原样传给它；worker 是 `pthread_create`
  起的裸线程，**不走** `spawn` 的回收链，所以 `stop()` 必须自己 join。

### `chan`：有界通道

```c
import std.container.chan;

char buf[16 * sizeof(int)] = { 0 };
Chan ch = { 0 };
chan_init(&ch, buf, 16, sizeof(int));

ch.send(&v);                     /* 满了就等 */
ch.recv(&out);                   /* 空了就等 */
ch.close_chan();                 /* 收完剩下的之后 recv 返回 0 */
```

* 元素是**字节块**（`esize` 指定一个元素多少字节），`send`/`recv` 各拿一个指针，
  拷进去的是值；`int`、指针、结构体都能过通道。
* `send` 满则阻塞、`recv` 空则阻塞；`try_send`/`try_recv` 不阻塞；`send_ms`/
  `recv_ms` 限时（1 成功 / 0 超时 / -1 已关）。
* `close_chan()` 幂等：关闭后 sender 立刻拿到 0（`send_ms` 是 -1），receiver 把
  缓冲里剩下的取完之后 `recv` 返回 0（`recv_ms` 是 -1）。
* 状态查询 `count`/`capacity`/`is_empty`/`is_full`/`is_closed`，累计统计
  `sent_count`/`recv_count`。
* 正文依赖 `std.os.sync`（Mutex + 两个 Cond），编程序时那份 `.c6` 也要在命令行上。

### `tty`：终端检测、窗口大小与原始模式

```c
import std.os.tty;

if (is_tty(0)) {
    int rows = 0, cols = 0;
    size(0, &rows, &cols);           /* TIOCGWINSZ */
}

TtyMode mode = { 0 };
mode.raw(0);                         /* 关掉行缓冲/回显/信号键 */
defer mode.restore();
```

* `is_tty(fd)`（`isatty`）；`size(fd, &rows, &cols)`/`width`/`height`（取不到给 0，
  失败不动输出参数）；`set_size(fd, rows, cols)` 用 `TIOCSWINSZ`（伪终端/测试）。
* `TtyMode`：`raw(fd)` 保存整个 `termios` 再 `cfmakeraw` + `tcsetattr`，返回 1/0；
  `restore()` 恢复（重复恢复返回 0）；`is_active()`。对非终端 `raw` 失败且不改状态。
* 进程被信号打断时终端不会自动恢复：`raw` 之后请 `defer restore()`；装了信号
  处理的话在 `errdefer` 里也恢复一次。
* 只做本地终端；伪终端分配（`posix_openpt` 等）直接调 libc。

### `signal`：处理函数、屏蔽与发送

```c
import std.os.signal;

install(SIGINT, &on_ctrl_c);         /* SA_RESTART */
ignore(SIGPIPE);
block(SIGUSR1);
unblock(SIGUSR1);
send_self(SIGUSR1);                  /* raise */
```

* `install(sig, fn)`（`sigaction` + `SA_RESTART`：慢系统调用自动重来）、`ignore`、
  `reset`（恢复默认动作——**之后再收到就按系统默认处理**）。
* `block`/`unblock`/`is_blocked`/`is_pending` 改/查**当前线程**的屏蔽字：屏蔽期间
  发的信号挂起，解除时补投。
* `send_self(sig)` 是 `raise`、`send_to(pid, sig)` 是 `kill`（`pid>0` 单个、`0`
  同进程组、`-1` 所有有权限的）。
* `signal_name(sig)`/`signal_number("SIGTERM")` 名字↔编号，带不带 `SIG` 都认，
  未知给 0。
* 处理函数里只做异步信号安全的事：置 `volatile` 标志，不 `printf`、不分配、不加锁。

### `pipe`：匿名管道

```c
import std.os.pipe;

Pipe p = { 0 };
p.init();                            /* init_nonblock() 非阻塞 */
p.write_bytes("hello", 5);
p.close_write();                     /* 读端取完之后看到 EOF */
char buf[32] = { 0 };
int n = p.read_bytes(buf, sizeof(buf));
p.close_all();
```

* `read_bytes`：`>0` 字节数、`0` EOF、`-1` 错误（非阻塞下紧跟 `would_block()`
  判断是不是 EAGAIN）；`read_exact` 读满 `len` 或到 EOF。
* `write_bytes` 返回实际写进去的字节数（管道满/非阻塞时可能小于 `len`），`-1`
  一个都没写进去；管道缓冲（Linux 64 KiB）满了写端会阻塞。
* `close_read`/`close_write`/`close_all`，重复关返回 0；**写端不关，read 永远
  等不到 EOF**。`read_fd`/`write_fd`/`is_open` 查状态。
* 管道没有消息边界；要边界用 `std.container.chan` 或自己加长度前缀。

### `poll`：同时等多个 fd

```c
import std.os.poll;

if (wait_readable(fd, 1000))         /* 1 就绪 / 0 超时 / -1 出错 */
    ;

PollSet set = { 0 };
set.add(fd_a, EVENT_READ);
set.add(fd_b, EVENT_READ | EVENT_HUP);
if (set.wait(500) > 0) {
    int i = 0;
    for (i = 0; i < set.count(); i++)
        if (set.is_ready(i)) { ... }
}
```

* `PollSet` 里是 `POLL_MAX`（32）个 `struct pollfd`，不分配；`add` 同一个 fd 第二次
  是改事件（返回原槽位），满了返回 -1；`modify`/`remove_fd`/`find`/`count`/`fd_at`。
* `wait(timeout_ms)`：`<0` 永远等、`0` 立刻、`>0` 毫秒；返回就绪个数（0 超时、
  -1 出错），被信号打断会重试；`is_ready(i)`/`ready(i)` 看结果，`clear_ready()` 清。
* `EVENT_HUP`/`EVENT_ERROR` 也算就绪：对端关掉时给 `EVENT_HUP`（还有数据会同时给
  `EVENT_READ`），接着 `read` 就是 EOF。
* 单 fd：`wait_readable`/`wait_writable`/`wait_any(fd, events, ms)` 返回 1/0/-1。
* 只等不读写：就绪之后该 `read`/`write`（`std.os.io`、`std.os.pipe`、`std.net.tcp`）。

### `Range`：能被 `foreach` 吃掉

```c
import std.container.range;

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

它同时用到 `std.core.types`、`std.core.err`、`std.text.str`、`std.container.vec`、`std.container.span`、
`std.text.fmt`、`std.container.range` 和自己写的模块 `wordcount.text`
（`examples/wordcount/text.c6`，说明模块边界怎么划、`-I` 怎么给）。`std.container.vec`
的排序内核来自 `std.container.sort`——`import` 只带声明，所以 `std/container/sort.c6` 的正文也
必须一起编（背后一共 8 个 `std` 模块）。

### 集成示例：`examples/stdtour.c6`

```sh
make std-tour
```

```
stdtour: 标准库新模块一日游
strbuf  -42 deadbeef             std/container/vec.c6 ...
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
迭代器。示例里 `set` 依赖 `std/container/map.c6` 的正文，所以 `STDKIT_SRCS` 把它列在
`std/container/set.c6` 前面。

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
求摘要。`STDBOX_SRCS` 里 `std/data/json.c6` 要带上 `std/core/mem.c6`、`std/text/str.c6`、
`std/text/num.c6` 的正文，`std/encoding/sha256.c6` 要带上 `std/core/bits.c6`（`rotr32` 是跨 TU
调用）。

## 写法约定（照着写就不会踩坑）

### 标准库 API 形态：硬性要求

新增或改造 `std/` 模块时，**能用结构体方法表达的 API 必须写成方法**：

1. 第一个参数是 `T *`（或 `T` 值）、语义上在操作 `T` 的函数，一律按方法注册；
   调用侧只写 `v.m(...)` / `p->m(...)`，不写 `m(&v, ...)` / `m(p, ...)`。
2. 方法名用裸动词，不加 `reader_`/`writer_`/`json_`/`inet_` 之类的接收者前缀；
   同名由接收者 tag 区分（`Reader.close` / `Writer.close`、`BitWriter.align` /
   `BitReader.align`）。
3. 只有下面这些情况保留自由函数：
   - 工厂/构造：`reader_open`、`arena_make`、`csv_reader`、`ini_open`、`bw_open`；
   - 没有接收者的算法/编解码：`path_join`、`b64_encode`、`fnv1a_bytes`、`sort_int`；
   - 一次性便捷入口：`sha256`、`sha256_hex`、`hmac_sha256`；
   - 接收者不是第一个参数，或需要显式多态的：`uuid_v7_at(ms, r)`。
4. `foreach` 迭代器的 `has`/`next` 是语言协议，必须保留字面名，不受第 2 条影响。
5. 新增/修改的 `test` 块、`examples/std*.c6` 和 `docs/stdlib.md` 示例必须用方法
   语法；评审时把 `m(&v, ...)`、带接收者前缀的方法名、文档里的函数式调用视为
   不合格。

* **普通结构体的方法就是「第一个参数是结构体指针」的文件作用域函数**：
  `int count(Str *self)`；泛型结构体才能在 `struct` 体内写方法体
  （`struct Split { int has() { ... } }` 会被拒：`a function cannot be defined
  inside a struct`）。
* **普通结构体的方法名直接用动词，不加接收者前缀**（`read_line`、`flush`、
  `write`、`alloc`、`below`、`update`、`next`、`format`）。体内写不了方法体，
  方法写成体外函数；`std` 的每个文件都是 module，裸名弱别名带模块前缀，不会
  进全局命名空间，同一模块里不同结构体的同名方法也按 `<Tag>_<name>` 区分
  （`Reader.close` 与 `Writer.close` 可以共存）。调用侧统一写
  `pr->read_line(...)` / `v.write(...)`；工厂和一次性函数仍是自由函数
  （`reader_open`、`rand_seed`、`bw_open`）。
* **例外：`foreach` 的迭代器协议只认字面上的 `has()` / `next()`**
  （`tcc/tccgen.c` 的 `c6_iter_proto_of` 用 `tok_alloc_const("has"/"next")`
  找方法），所以 `Split`/`Lines`/`Fields`/`Range`/`StepRange`/`CFile` 之外的
  迭代器不能把这两个方法改名；名字必须是字面上的 `has`/`next`，否则
  `foreach` 会报「no 'has'/'next' iterator」。同名的 `has`/`next` 分属不同
  tag 时弱别名只保留一个，但方法调用（`it->has()`）按接收者解析，不受影响。
* 值容器调方法写 `v.m()`（或 `(&v)->m()`）；`@T` 托管的变量直接 `v->m()`。
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
  要改视图就按视图重建：`(char[:]){ p.data, n }`（`std/os/path.c6` 的
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
| **普通**（非泛型）结构体不能在 `struct` 体内写方法体（`a function cannot be defined inside a struct`） | 方法写成「第一个参数是结构体指针」的文件作用域函数，调用写 `v.m()`（或 `(&v)->m()`） |
| 文件作用域里写「参数/返回类型带实例类型」的辅助函数，会让**导入这个模块的每个 TU** 都提前实例化该类型 | 别这么写；需要时把逻辑放进 `test` 块或用标量参数（`std/text/fmt.c6` 的测试用 `goto` 取错误码）。在导入方写 `static int f(Set@int *s)` 再调 `s->has()` 还会得到 pointer/integer 警告（返回值类型没解析对），见上面「写法约定」 |
| ~~往 `Buf@(N)` 里加若干形状相同的方法会挂~~（曾经的 `skip(')')` → `end_macro()` 死循环） | **已消失**：同形状方法（`char tmp[16]` + 进/移位 + `(char)` 强转）加到五个也编得过、跑得通，`std/text/fmt` 的 `write_u64`/`write_hex`/`write_bin`/`write_bool`/`write_ptr` 就是照这个形状补的（`tccpp.c` 的宏重放改写之后不再复现） |
| ~~把 `Buf@(N)` 的 `write_hex` 形参从 `u32` 加宽到 `u64` 会**挂住编译器**~~（当时 `-c` 无输出直到超时，退出码 124） | **现在复现不出来**：当前 `std/text/fmt.c6` 的 body、旧版 body、带 `(char)` 强转的写法都在 1 秒内编过，`-test` 也是 10 个块全过。`write_hex(u32)`/`write_hex64(u64)` 双入口保留只是为了不动 API |

这些限制在 `docs/reference/grammar.md`（约束表 C1xx）和 `docs/design/plan.md` 里有对应的
条目；标准库的写法就是它们当前的「推荐用法」。

### 集成示例：`examples/stdmore.c6`

```sh
make std-more
```

```
stdmore: 标准库第七批（bitset / glob / base32 / http）
stdmore: 28 check(s), 0 failure(s)
```

四个模块各一节：bitset 的置位/计数/按位遍历/末字掩码、glob 的 `*`/`?` 与路径
模式、base32 的 RFC 向量与非法输入、http 的请求/响应/头字段/body。它和
`stdtest` 一样，把「模块正文要单独列在命令行上」这件事钉在 Makefile 里。

### 集成示例：`examples/stdnext.c6`

```sh
make std-next
```

```
stdnext: 标准库第八批（base58 / varint / semver / cookie）
stdnext: 23 check(s), 0 failure(s)
```

四节分别是 base58 的已知向量/前导零、varint 的 300/负数往返、semver 的经典优先级
序列、cookie 的属性与 Max-Age。和 `stdmore` 一样，Makefile 里显式列出模块正文。

### 集成示例：`examples/stdnode.c6`

```sh
make std-node
```

```
stdnode: 标准库第九批（list / ring / utf16 / ansi）
stdnode: 17 check(s), 0 failure(s)
```

四节分别是侵入式链表的 push/迭代/remove、环形队列的回绕、UTF-16 代理对与非法
代理、ANSI SGR/256 色/光标移动。Makefile 里照旧显式列出四个模块正文。

### 集成示例：`examples/stdform.c6`

```sh
make std-form
```

```
stdform: 标准库第十批（props / multipart / punycode / width）
stdform: 26 check(s), 0 failure(s)
```

四节讲同一条链路：props 读 `.env` 配置（三种分隔符、`export`、续行、`\u` 解码、
空键报错）→ multipart 收表单（文本字段与文件字段、头字段名大小写不敏感）→
punycode 把 `münchen.de`/`中文.cn` 转成 A-label 再转回来 → width 按列宽裁剪、
补齐、做宽字符列对齐。Makefile 里照旧显式列出四个模块正文，外加
`std/text/utf8.c6`（width/punycode 用它的 `utf8_decode`；`Str` 类型只靠 `import`
带来的声明，不需要 str 的正文）。

### 集成示例：`examples/stdembed.c6`

```sh
make std-embed
```

```
stdembed: 标准库第十一批（crc / fixed / pool / frame）
stdembed: 30 check(s), 0 failure(s)
```

四节串成一条遥测链路：fixed 把采样值变成 Q16.16/Q15（乘除、开方、sin、饱和）→
crc 算 Modbus 请求的 CRC-16、1-Wire ROM 的 CRC-8、CRC-32C 与 Fletcher/LRC/NMEA
异或 → pool 用固定块收样本（结构体直接放进块、释放后按 LIFO 复用）→ frame 把
负载编码成 COBS 帧和 SLIP 帧再解回来（含「输出缓冲不够时一个字节都不写」与
「负载里未转义的 END 要报错」）。Makefile 里显式列出四个模块正文，它们都只
依赖 `std/core/types.c6`。

### 集成示例：`examples/stdtext.c6`

```sh
make std-text
```

```
stdtext: 标准库第十二批（regex / diff / fuzzy / wrap）
stdtext: 35 check(s), 0 failure(s)
```

四节：regex 用 `^(\\S+) (ERROR|WARN|INFO): (.*)$` 把四条日志拆成时间/级别/消息并
数 ERROR，再用 `\\d+` 的 `find_all` 扫 `a1 bb22 ccc333` 的三段数字，最后钉住
「少右括号」的编译错误位置；fuzzy 对 7 条子命令做「instal → install」的建议、
kitten/sitting 的编辑距离、换位距离、千分比与子序列打分（`updt` 最像 `update`）；
wrap 把中英混排的帮助文本折成 34 列（首行缩进 2、续行 6），逐行验宽度 ≤ 34，
再用 `text_center` 居中标题；diff 给两份 4 行配置的 `@@ -1,4 +1,4 @@`（ctx=1），
并验「放不下返回 -1」。Makefile 里显式列出八个模块正文（regex/fuzzy 依赖
`std/text/utf8.c6`，wrap 再依赖 `std/text/width.c6`，diff 依赖 `std/core/mem.c6`）。

### 集成示例：`examples/stdnum.c6`

```sh
make std-num
```

```
stdnum: 标准库第十三批（bignum / decimal / rational / complex）
stdnum: 21 check(s), 0 failure(s)
```

四节：「大整数」算 30!（33 位）与 2^100，用 `divmod` 验 `30! / 29! == 30`，再做
`deadbeef` 的十六进制解析/输出与 1024 位溢出；「十进制」算 `19.99 * 3` 的小计、
6.5% 税额按分四舍五入、合计，并钉住 `0.1 + 0.2 == 0.3` 与 `1/3` 的 9 位结果；
「有理数」累加调和级数 `H_10 = 7381/2520`（再转 f64 复核），并比较交叉相乘会溢出的
两个大分数；「复数」算 `(3+4i)(1-2i) = 11-2i`、`|3+4i| = 5`、`(3+4i)^2`、共轭乘积
与除零的 IEEE 行为。Makefile 里显式列出六个模块正文（complex 依赖 `std/core/math.c6`）。

### 集成示例：`examples/stdpack.c6`

```sh
make std-pack
```

```
stdpack: 标准库第十四批（lz4 / rle / base85 / hexdump）
lz4: 1024 -> 34 字节（3%）
rle: 256 字节采样 -> 差分 256 -> RLE 6 字节
---- hexdump(width=8, base=0x1000) ----
00001000  68 65 78 64 75 6d 70 20  |hexdump |
00001008  72 6f 63 6b 73 21 68 65  |rocks!he|
00001010  78 64 75 6d              |xdum    |
stdpack: 24 check(s), 0 failure(s)
```

四节：「压缩」把 1024 字节的重复遥测帧压到 34 字节再解回来（并验偏移 0 的坏块被
拒绝）；「游程」对 256 个缓变采样先差分再 RLE（6 字节），再解回原采样（并验截断
的字面量被拒绝）；「文本编码」钉住 Ascii85 的 `hello → BOu!rDZ` 与 Z85 的标准向量
`HelloWorld`，再做 37 字节任意二进制的往返；「十六进制」按 8 字节一行、偏移从
0x1000 起排版并反向解析，另验纯 hex 输入与大写/无 ASCII 列。Makefile 里显式列出
六个模块正文（四个模块都只依赖 `std/core/types.c6`）。

### 集成示例：`examples/stdconf.c6`

```sh
make std-conf
```

```
stdconf: 标准库第十五批（toml / xml / query / template）
toml: 6 条，title=demo host=localhost port=8080
xml: 1 个开始 item、1 个自闭合、2 段文本
query: q=hello world
query: 构建 = q=hello+world&page=3
template: [demo] localhost:8080 (debug=off)
stdconf: 19 check(s), 0 failure(s)
```

四节：「配置」读一段带注释、表、数组、布尔与整数的 TOML，用 `decode`/`toml_int`/
`toml_bool`/`toml_array_at` 取出 title/host/port/ports/debug；「文档」扫一段带属性、
自闭合元素与实体的 XML，用 `attr` 取 id、用 `xml_unescape` 把 `&amp;` 解回来；
「查询串」从 `q=hello+world&page=2&tag=%E4%B8%AD` 里取解码后的值，再把两个参数重新
构建成 `q=hello+world&page=3`；「模板」把前面拿到的值填进
`[{{title}}] {{host}}:{{port}} (debug={{debug}})`，并验严格/宽松两种缺变量行为。
Makefile 里显式列出九个模块正文（toml 依赖 str+num+err，其余只依赖 str）。

### 集成示例：`examples/stdcli.c6`

```sh
make std-cli
```

```
stdcli: 标准库第十六批（args / table / natural / proc）
args: 1 个开关、2 个选项、3 个位置参数（port=8080）
natural: file1 < file2 < file10 < file20
natural: 1536 字节 = 1.5 KiB
natural: 3723000ms = 1h 2m 3s
table:
+--------+---------+
| file   |    size |
+--------+---------+
| file1  | 1.5 KiB |
| file2  | 1.0 MiB |
| file10 |   512 B |
+--------+---------+
proc: sh -c exit 5 -> exited=1 code=5
proc: which sh = /usr/bin/sh
stdcli: 17 check(s), 0 failure(s)
```

四节：「参数」用规格表解析 `-v --port 8080 --name=demo a.txt b.txt -- -x`（并验
未知选项报错）；「自然」用 `natural_cmp` 给 file1/file2/file10/file20 排序，再把
1536 字节、3723000ms、1234567 格式化成人类可读；「表格」把文件名与大小排成对齐的
两列表（右对齐），并验长度与内容；「子进程」跑 `sh -c "exit 5"` 看退出码、验
`kill -TERM $$` 的信号终止，最后在 PATH 里找 `sh`。Makefile 里显式列出九个模块
正文（args 依赖 str、table 依赖 width+utf8，natural 与 proc 只依赖 types）。

### 集成示例：`examples/stdwire.c6`

```sh
make std-wire
```

```
stdwire: 标准库第十七批（sha1 / ws / dns / tcp）
sha1: sha1(abc) = a9993e364706816aba3e25717850c26c9cd0d89d
ws: accept = s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
ws: 帧 11 字节，解出 "Hello"
dns: example.com A 93.184.216.34 ttl=300
tcp: 监听端口 35579
tcp: 往返 4 字节
stdwire: 19 check(s), 0 failure(s)
```

四节：「摘要」钉住 `abc` 的 SHA-1 与 RFC 2202 的 HMAC 向量；「握手/分帧」用
RFC 6455 的 `Sec-WebSocket-Accept` 向量，再编码一帧带掩码的文本并解回来；
「DNS」构建 example.com 的 A 查询，再解一段带压缩指针的响应（地址、TTL）；
「TCP」在 127.0.0.1:0 上监听、连接、接受、双向收发，并验超时返回 -1、对端关闭
返回 0。Makefile 里显式列出八个模块正文（ws 依赖 sha1+base64+err，其余只依赖
types）。

### 集成示例：`examples/stdalgo.c6`

```sh
make std-algo
```

```
stdalgo: 标准库第十八批（graph / lru / trie / region）
graph: 拓扑序 0 2 1 3 4
graph: 到 1/2/3 的最短距离 = 3/1/4（经 2,1）
lru: size=3 hits=4 misses=2 evicts=1
trie: 5 个命令、18 个节点；"git " 前缀命中
region: 1 段、共 0x3000 字节，空隙在 0x2000
stdalgo: 26 check(s), 0 failure(s)
```

四节：「图」给一条构建依赖链做拓扑排序（并验每条边都满足顺序），再在带权图上
跑 Dijkstra（距离 3/1/4 与前驱链）；「LRU」容量 3，命中/未命中/淘汰后检查统计；
「前缀树」放 5 条命令，查 `git`/`git commit` 与 `"git "` 前缀，再删 `go` 确认
`gofmt` 还在；「区间」加两段地址、查半开区间的边界、找 0x2000 处的空隙，再把
两段桥接成一段。Makefile 里显式列出六个模块正文（四个模块都只依赖
`std/core/types.c6`）。

### 集成示例：`examples/stdthread.c6`

```sh
make std-thread
```

```
stdthread: 标准库第十九批（atomic / sync / thread / chan）
atomic: 计数=20000，自旋锁求和=2000
sync: 计数=2000，信号量=1
thread: 4 个 worker 跑完 4 个任务，1..400 的和=80200
chan: 100 个元素全部过通道，和=5050
stdthread: 29 check(s), 0 failure(s)
```

四节：「原子」四个线程各 `inc` 5000 次（必须正好 20000），再用 `SpinLock` 护住
普通变量做 2000 次自增；「同步」两把锁保护计数、条件变量握手（worker 先在锁里
置 ready 并 signal，主线程等到之后置 go 再 signal 让它继续）、读写锁的
「有读者时拿不到写锁」、信号量的 P/V 计数；「线程池」4 个 worker 分四段算
1..400 的和（80200），查完成计数与 `stop` 之后不再接活；「通道」容量 4 的通道
上一个生产者发 1..100、一个消费者求和（5050），关掉通道后消费者退出。
Makefile 里显式列出六个模块正文（chan 依赖 sync，其余各自只依赖 types）。

### 集成示例：`examples/stdterm.c6`

```sh
make std-term
```

```
stdterm: 标准库第二十批（tty / signal / pipe / poll）
tty: 伪终端 40x120，原始模式写入/读取/恢复都对
signal: SIGUSR1 收到 2 次，屏蔽期间挂起后补投
pipe: 5 字节往返、EOF、非阻塞 EAGAIN 都对
poll: 2 个 fd 里第 2 个先就绪，单 fd 超时正常
stdterm: 45 check(s), 0 failure(s)
```

四节：「终端」`posix_openpt` 开一对伪终端，`set_size(40, 120)` 之后读回、`width`/
`height` 对得上，`raw` 之后从主端写两个字节、从端立刻读到，再 `restore`（环境
没有 `/dev/pts` 时只跳过这一节，管道不是终端那两条照样跑）；「信号」装上
`SIGUSR1` 处理函数收一次、`ignore(SIGUSR2)` 不跑处理函数、屏蔽 `SIGUSR1` 之后
发信号只挂起不投递、`unblock` 补投一次、名字↔编号互转；「管道」写 5 字节读回来、
关写端读到 EOF、非阻塞下 `EAGAIN`；「轮询」两个管道 fd 都没数据时 `wait(0)`
返回 0，往第二个写一个字节之后 `wait(200)` 只有一个就绪、`ready(1)` 带
`EVENT_READ`，单 fd 的 `wait_readable` 超时/就绪都对。Makefile 里显式列出六个
模块正文（tty/signal/pipe/poll 都只依赖 `std/core/types.c6`）。

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
* `make std-more`：`examples/stdmore.c6`，第七批模块的拼装示例（自带自检）。
* `make std-next`：`examples/stdnext.c6`，第八批模块的拼装示例（自带自检）。
* `make std-node`：`examples/stdnode.c6`，第九批模块的拼装示例（自带自检）。
* `make std-form`：`examples/stdform.c6`，第十批模块的拼装示例（自带自检）。
* `make std-embed`：`examples/stdembed.c6`，第十一批（嵌入式基础）模块的拼装示例
  （自带自检）。
* `make std-text`：`examples/stdtext.c6`，第十二批（文本算法）模块的拼装示例
  （自带自检）。
* `make std-num`：`examples/stdnum.c6`，第十三批（数值类型）模块的拼装示例
  （自带自检）。
* `make std-pack`：`examples/stdpack.c6`，第十四批（压缩与文本编码）模块的拼装
  示例（自带自检）。
* `make std-conf`：`examples/stdconf.c6`，第十五批（配置与文档格式）模块的拼装
  示例（自带自检）。
* `make std-cli`：`examples/stdcli.c6`，第十六批（命令行工具基础）模块的拼装
  示例（自带自检）。
* `make std-wire`：`examples/stdwire.c6`，第十七批（网络协议基础）模块的拼装
  示例（自带自检）。
* `make std-algo`：`examples/stdalgo.c6`，第十八批（算法与容器）模块的拼装
  示例（自带自检）。
* `make std-thread`：`examples/stdthread.c6`，第十九批（并发与同步）模块的拼装
  示例（自带自检）。
* `make std-term`：`examples/stdterm.c6`，第二十批（终端与进程控制）模块的拼装
  示例（自带自检）。
* `std/net/inet.c6` 的「RFC 5952 压缩」用例：8 组输入对 8 组期望输出，含
  `2001:db8:0:0:1:0:0:1`、同长零段取第一个（`1:0:0:2:0:0:3:4`）、
  单组零不压（`2001:db8:0:1:1:1:1:1`）与只有单零段的 `0:0:1::`。
* `std/encoding/bitbuf.c6` 的「整段字节」用例：写/读两侧共用一套边界，钉住
  `put_bytes`/`get_bytes` 的起始下标（曾经在循环里用增长中的游标当下标，
  第二字节写到了下一格）。
* `std/container/pqueue.c6` 的「交错 push/pop」：100 次混合操作，每步都跑 `is_valid()`，
  钉住 sift_up/sift_down 的四种交换方向。
* `std/net/url.c6` 的「RFC 3986 §5.4 的相对解析」：23 条 normal + abnormal 用例
  （`../..//g`、`g;x=1/../y`、结尾 `/.` 与 `/..` 都在里面），钉住 `remove_dots`
  的每个分支；另有「全零初始化的 `Url` 不该给出端口」的回归。
* `std/encoding/uuid.c6` 的 v5 已知向量：与 Python `uuid.uuid5` 逐字节一致
  （`www.example.com` / `python.org` / `www.widgets.com`）。
* `std/container/map.c6` 的「墓碑不会在扩容时复活」：删除留下墓碑后继续插入触发扩容，
  验证墓碑不会被搬进新表（写 `std/container/set` 时踩到的真实 bug）。

* `std/encoding/punycode.c6` 的向量：RFC 3492 的德语/中文/日语/韩语/俄语/阿拉伯语
  样本逐字节对过 Python `str.encode('punycode')`，IDNA 的
  `münchen.de`/`中文.cn`/`пример.рф` 与 `idna` 编解码一致；另有「纯 ASCII 带
  结尾 `-`」「63/255 字节上限」「空标签/坏 punycode 被拒」的回归。
* `std/net/multipart.c6` 的「伪分隔符」用例：`--bX` 这种不是分隔符的行要当
  preamble 跳过（boundary 是另一个分隔符的前缀时最容易踩），钉住
  `mp_find_delim` 的行尾判定；另有「无头 part」「preamble/epilogue」「LF 行尾」
  三条。
* `std/data/props.c6` 的「续行」用例：行尾奇数个反斜杠才续行（偶数个是字面
  反斜杠），`value` 视图跨过原始 `\`+换行，解码时合并；另钉住 `\u` 代理对与
  落单代理的 -1。
* `std/text/width.c6` 的判宽表：`中`/`Ａ`/`🙂` = 2 列、`U+0301` 组合符与
  `U+1160` 谚文中声 = 0 列、控制字符 0 列（严格版 -1）、非法字节 1 列；
  `text_prefix_len` 不切半个 UTF-8 序列。
* `std/core/crc.c6` 的 check 值：CRC-8/SMBUS `0xf4`、CRC-8/MAXIM `0xa1`、
  CRC-16/CCITT-FALSE `0x29b1`、XMODEM `0x31c3`、MODBUS `0x4b37`、ARC `0xbb3d`、
  CRC-32C `0xe3069283`，CRC-32/IEEE 的裸寄存器值 `0x340bc6d9`（xorout 前）；
  另有「分三段喂 == 一次喂完」「空输入给 init」「Fletcher-16 的空输入是 0」。
* `std/encoding/frame.c6` 的 COBS 边界：254 个非零字节必须编成 256 字节
  （`0xff` 后跟一个空组），`cobs_encoded_len` 对含 0 的 300 字节输入给出精确
  长度而不是上界；坏输入（`0x00` code、截断的 code、负载里的 0）与坏 SLIP
  转义/负载里的 END 都要失败，且失败时目标缓冲保持原样。
* `std/core/pool.c6` 的「未对齐缓冲」：`base` 必须向上对齐后才能发块，块大小
  向上取整（20 → 32）；`free` 拒绝池外/不对齐指针，`reset` 之后从第一块重新
  开始发；坏参数得到容量 0、`alloc` 永远返回 0 的池。
* `std/core/fixed.c6` 的边界：Q16.16 的 `round`/`floor`/`ceil`/`frac` 对负数
  的语义、`mul`/`add` 的饱和（`30000 * 2` → `I32_MAX`）、`sqrt(4)` 精确为 2、
  Q15 的 `(-1.0) * (-1.0)` 饱和到 32767；`sin`/`cos` 在 π/2 的整数倍处取精确值。
