# 变更史

> **状态**：现行 · **索引**：[文档索引](docs/README.md) · **实施状态**：[docs/history/status.md](docs/history/status.md)
>
> 这里是**按特性归档的变更史**（不是逐条 commit 日志）。每条给一个代表性提交的短
> hash，完整历史用 `git log` 查。日期取该特性的收尾提交。

## 未发布

### 文档

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

### 标准库

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
  `bw_patch` 支持不移动游标的回填；pqueue 是托管二叉堆，比较器与 `std.container.sort` 通用，
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
