# 变更史

> **状态**：现行 · **索引**：[文档索引](docs/README.md) · **实施状态**：[docs/history/status.md](docs/history/status.md)
>
> 这里是**按特性归档的变更史**（不是逐条 commit 日志）。每条给一个代表性提交的短
> hash，完整历史用 `git log` 查。日期取该特性的收尾提交。

## 未发布

### 文档

- **标准库 API 形态要求**：`docs/stdlib.md` 的写法约定增加硬性条款——能用结构体方法
  表达的 API 必须写成方法，调用侧只写 `v.m()`/`p->m()`；自由函数只保留工厂、无接收者
  算法、一次性便捷入口和 `foreach` 的 `has`/`next`。
- **文档 IA 重构**：`README.md` 从 1996 行瘦身为落地页（特性总表 + 快速开始 + 目录结构 +
  文档地图）；「使用」章拆成 `docs/manual/` 一特性一篇（14 篇，统一「状态 → 一句话 →
  例子 → 速查 → 陷阱」模板）。
- **新增参考文档**：`docs/reference/diagnostics.md`（C1–C134 约束与诊断文本，
  从 `grammar.md` §7 抽出）、`docs/reference/cli.md`（编译器选项 + 全部 make 目标 +
  环境变量）、`docs/reference/c99-backend.md`（C99 后端，原文为英文，已中文化）、
  `docs/design/implementation-notes.md`（运行时/ABI + `grammar.md` §11 实现位置表）、
  `docs/history/status.md`（`plan.md` §11 状态表）、`docs/README.md`（三条读者路径 +
  维护约定）。
- **目录分层**：`docs/{manual,reference,design,history}/`；全仓库 `docs/...` 引用同步更新
  （含 C 源码注释），零断链。
- **去重复**：模块数 / `test` 块数 / 自检条数不再散落在 README、`stdlib.md`、
  `status.md` 多处，改为「以 `make stdtest` 输出为准」；状态数字只在 `status.md` 留快照。
- **修正过期状态**：`gc-design.md` 的「设计定稿待编码」改为已落地（P0–P8）。
- **`make help`** 从打印 20 行注释改为打印完整目标清单（37 个目标，按用途分组）。

### 语言与编译器

- **`!error` 支持浮点返回类型**：`float` / `double` / `long double` 现在可以作
  `!error` 的返回类型（C9 原先直接拒绝）。失败路径的返回值仍然只承载成功路径、
  永远不可观测：整数类标量继续用 `-1` 哨兵，聚合类型和浮点标量改为清零（全零
  位型对 IEEE 浮点就是 `+0.0`），native 与 C99 后端、普通函数与 `async` 体走
  同一条规则。数组不是 C 的返回类型，`A!error f(void)` 仍在声明处报
  `'!error' does not support a 'int [3]' return type`。`tests/try.c6` 加了
  double / float / long double 的成功、失败与 `try` 传播用例，
  `tests/errors/try.c6` 把原来的 float 拒绝用例换成数组拒绝用例；
  `docs/manual/errors.md`、`grammar.md` R22、`diagnostics.md` C9 同步。

### 标准库

- **标准库方法化**：`std/` 的普通结构体方法去掉 `reader_`/`writer_`/`cfile_`/`sha256_`/
  `arena_`/`random_`/`uuid_`/`inet_` 之类的接收者前缀，统一成 `pr->read_line(...)`、
  `b->write(...)`、`r.next_u64()`、`a.json_parse(...)`、`ip.is_private()`。方法符号
  仍是 `<Tag>_<name>`；模块里的裸名弱别名带模块前缀，所以 `close`/`ok`/`next`/`format`
  这些同名方法在不同结构体、不同模块之间可以共存，`foreach` 的 `has`/`next` 保持
  字面名不变。工厂和一次性函数（`reader_open`/`rand_seed`/`bw_open`/`csv_reader`/
  `ini_open`）仍是自由函数。`std/`、`examples/std*.c6`、`docs/stdlib.md`、`README.md`
  已全部改用方法语法。

- **标准库第十一批（嵌入式基础）**：`std.core.crc` / `std.core.fixed` /
  `std.core.pool` / `std.encoding.frame` 四个模块，集成示例
  `examples/stdembed.c6`（`make std-embed`）。crc 补 CRC-8（SMBUS/1-Wire）、
  CRC-16（CCITT-FALSE/XMODEM/MODBUS/ARC）、CRC-32C 与 Fletcher-16/LRC/XOR/
  sum8/sum16，全部逐位不带表并带流式 `*_update`；fixed 提供 Q16.16 的 `Fix`
  （饱和四则、开方、Taylor sin/cos，误差 < 1e-5）与 Q15 的 `Q15`（饱和乘法）；
  pool 是借用调用方缓冲的定长块分配器（O(1) alloc/free、块对齐、坏指针拒绝）；
  frame 是 COBS 与 SLIP（RFC 1055）的零分配分帧/转义（先量再写，坏格式不写
  半截）。四个模块都只依赖 `std.core.types`，不链 libc/libm、不分配，可以直接
  用在 freestanding/裸机程序里。错误号段 `std.core.crc`=0x6035 …
  `std.encoding.frame`=0x6038（`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库第十批**：`std.data.props` / `std.net.multipart` / `std.encoding.punycode` /
  `std.text.width` 四个模块，集成示例 `examples/stdform.c6`（`make std-form`）。
  props 读 `.properties`/`.env`（三种分隔符、`export`、续行、`\uXXXX` 与代理对、
  语法错误给行号，值视图保留转义原文、`decode` 才解）；multipart 零分配解析
  `multipart/form-data`（boundary 引号、伪分隔符当 preamble、无头 part、CRLF/LF）；
  punycode 覆盖 RFC 3492 与 IDNA 的 `xn--` 层（纯 ASCII 带结尾 `-`、63/255 字节
  上限、解码溢出检查）；width 用 Unicode 15.0 的 EastAsianWidth/通用类别表算
  终端列数并做裁剪/补齐（不做字素簇合并）。错误号段
  `std.data.props`=0x6031 … `std.text.width`=0x6034。

- **标准库第九批**：`std.container.list` / `std.container.ring` / `std.text.utf16` /
  `std.text.ansi` 四个模块，集成示例 `examples/stdnode.c6`（`make std-node`）。
  侵入式双向链表不拥有节点、remove 先确认归属；环形队列借用调用方数组、满时不
  覆盖；UTF-16 处理代理对/非法代理/截断；ANSI 构造器拼 SGR、256 色与光标移动，
  满了置 ok=0。错误号段 `std.container.list`=0x602d … `std.text.ansi`=0x6030。

- **标准库第八批**：`std.encoding.base58` / `std.encoding.varint` / `std.text.semver` /
  `std.net.cookie` 四个模块，集成示例 `examples/stdnext.c6`（`make std-next`）。
  Base58 走 Bitcoin 字母表、前导零写 `1`；varint 提供 LEB128 无符号/有符号
  （zigzag）编解码，解码区分成功/截断/非法；semver 严格按 SemVer 2.0.0 解析与
  比较优先级；cookie 零分配解析 Set-Cookie / Cookie:，属性名大小写不敏感。
  错误号段 `std.encoding.base58`=0x6029 … `std.net.cookie`=0x602c。

- **标准库第七批**：`std.container.bitset` / `std.text.glob` / `std.encoding.base32` /
  `std.net.http` 四个模块，集成示例 `examples/stdmore.c6`（`make std-more`）。
  bitset 借用调用方 `u64` 缓冲，末字多余位始终清零；glob 是 `Glob` 对象上的
  `match`/`match_path`，支持 `*`/`?`/字符类/转义；base32 覆盖 RFC 4648 标准表与
  base32hex，填充策略显式、拒绝非零填充位；http 零分配解析请求/响应，头字段名
  大小写不敏感、不接受 obs-fold。错误号段 `std.container.bitset`=0x6025 …
  `std.net.http`=0x6028；模块、示例、文档都按 `docs/stdlib.md` 的「API 形态：
  硬性要求」写成结构体方法。

- **标准库分目录**：`std/` 从平铺改成按领域分七个子目录。模块名就是路径，所以
  模块名多了一段（`std.vec` → `std.container.vec`、`std.err` → `std.core.err`）。
  分组：
  - `std/core/`：`types` `err` `mem` `math` `bits` `hash` `rand`
  - `std/text/`：`str` `strbuf` `fmt` `utf8` `ascii` `num`
  - `std/container/`：`vec` `span` `map` `set` `deque` `pqueue` `range` `sort`
  - `std/os/`：`io` `fs` `path` `env` `time`
  - `std/encoding/`：`codec` `base64` `sha256` `bitbuf` `uuid`
  - `std/data/`：`json` `csv` `ini`
  - `std/net/`：`inet` `url` `mime`

  编译器里的 std 错误号段表只改了名字，**槽位数值一个没动**（`std.core.err`
  仍是 0x6000），`errbase.def` 不动；`make stdtest` 改为遍历 `std/` 下的子目录。
- **标准库第三批**：`std.text.ascii` / `std.text.num` / `std.encoding.codec` / `std.core.rand` / `std.os.fs` /
  `std.container.set` 六个模块，集成示例 `examples/stdkit.c6`（`make std-kit`）；顺手修掉
  `std/container/map` 扩容复活墓碑的 bug（`std/container/set` 的对应用例钉住）。错误号段
  `std.text.ascii`=0x6013 … `std.container.set`=0x6018。
- **标准库第六批**：`std.net.inet` / `std.encoding.bitbuf` / `std.container.pqueue` / `std.data.ini` 四个模块，
  集成示例 `examples/stdnet.c6`（`make std-net`）。inet 的 IPv6 输出按 RFC 5952
  规范化（最长零段压缩、同长取第一个、v4-mapped 写点分），解析对前导零/多余冒号
  一律严格；bitbuf 是 MSB-first 的位读写器，越界只报 `FULL`/`SHORT` 而不写半截，
  `patch` 支持不移动游标的回填；pqueue 是托管二叉堆，比较器与 `std.container.sort` 通用，
  带 `push_pop`（top-k）与 `heapify`；ini 是零分配迭代读取器，坏行给准确行号。
  错误号段 `std.net.inet`=0x6021 … `std.data.ini`=0x6024。
- **标准库第五批**：`std.encoding.base64` / `std.net.url` / `std.encoding.uuid` / `std.net.mime` 四个模块，
  集成示例 `examples/stdweb.c6`（`make std-web`）。Base64 把「表」与「填充」变成显式
  参数（标准表/URL 安全表不能混用）；URL 是 RFC 3986 §5.2 的完整相对解析（§5.4 的
  23 条 normal + abnormal 用例钉死）；UUID 覆盖 v4（随机）/v5（SHA-1 名字派生，已知
  向量与 Python `uuid5` 一致）/v7（48 位毫秒时间戳）；MIME 是扩展名表 + 分类 + 魔数
  嗅探。错误号段 `std.encoding.base64`=0x601d … `std.net.mime`=0x6020。
- **标准库第四批**：`std.data.json` / `std.encoding.sha256` / `std.data.csv` / `std.os.env` 四个模块，
  集成示例 `examples/stdbox.c6`（`make std-box`）。JSON 是 arena 上的 DOM（数字保留
  原文、严格 RFC 8259、代理对解码）；SHA-256/HMAC 的 FIPS 180-4 与 RFC 4231 向量
  钉死在用例里；CSV 是零分配读取器 + 写调用方缓冲；env 管环境变量与 `argc/argv`。
  错误号段 `std.data.json`=0x6019 … `std.os.env`=0x601c。

## 2026-11-11

- **LLVM IR 后端**：按输出后缀 `.ll` 选择，复用整个前端 — `a6543f2`
- **交叉编译一条命令**：`--target=<triple>` / `--cc` / `--emit-llvm`，wasm 目标可跑 — `c36d1f6`
- **工具链改名**：构建产物统一叫 `tcc/c6`（原 `tcc/tcc`）
- **标准库 `std/` 首批**：一个文件一个 module，八个模块 + wordcount 集成示例 — `48c9193`
- **标准库扩充**：sort/mem/io/math/time + vec/str/map 增补 — `01ad893`
- **标准库第二批**：strbuf/utf8/bits/hash/path/deque，并加固 str/vec/fmt — `e5976f5`

## 2026-11-10

- **闭包 V2 收尾**：捕获式闭包 P0–P5 全部落地，文档与用例跟上 — `1364fe9`
- **`.c6` 后缀成为唯一开关**：删除 `-std=c6`，`.c` 文件永远按普通 C 编译 — `e066b5b`

## 2026-11-09

- **字符串插值**：`f".."` 是 `const char *`，`@f".."` 是托管对象 — `eb7df04`
- **引用计数 GC**：P0–P8 全流程 + 跨 TU 环检测 + 多环报告 — `e5b284e`
- **函数字面量**：`void(int x) { ... }` 是表达式，值就是函数指针 — `5404367`
- **闭包 P0/P1**：`closure` 类型、捕获扫描、标量捕获与调用改写 — `a077afd`
- **裸机**：自己写的 512 字节引导器把 c6 程序拉起来（`make fstest-bare`）— `455832c`

## 2026-11-08

- **module / import**：命名空间隔离，模块名就是它的路径 — `4be263a`

## 2026-11-07

- **元组**：`(T1, T2, ...)` 多返回值与解构 — `fd76ea9`
- **foreach**：数组 / 切片 / 整数区间遍历 — `ff1dc97`
- **async / await**：无栈协程 — `2e6fc87`
- **OS 线程**：`go` 改为真线程，协程派生改用 `co`（后来 `go` 更名为 `spawn`）— `6100ea8`
- **C99 输出后端**：`-c99` 把 c6 源码生成为可移植 C99 — `c2c279f`

## 2026-11-06

- **仓库初始化**：规范、补丁、构建脚本 — `8f77011`
- **变量必须初始化**：所有变量定义必须带初始化器（VLA 豁免）— `d113da7`
- **defer / errdefer / err**：用法说明与可运行示例 — `10a7e12`
- **切片 `T[:]`**：补丁 0011、文档与示例 — `fbab3dd`
- **结构体方法与 interface**：方法符号 `<Tag>_<meth>`，胖指针 + vtable — `7838182`
