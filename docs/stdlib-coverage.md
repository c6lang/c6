# 标准库覆盖面盘点（`std/` 到底还缺什么）

这份文档回答一个问题：**「继续扩 `std/`，什么时候才算到头？」**

结论先给：到目前为止（377 个模块、九十一批、`cmake --build build --target stdtest` 377/377 全绿），
**按族覆盖已经没有空缺**，新增模块只能在同一族里造变体，或者必须引入外部数据
（词典）与大运行时（QUIC/TLS 1.3 完整栈）。所以本轮的收尾动作是：把覆盖面写成
这份可核对的矩阵，而不是继续堆数量。

---

## 1. 规模与分布

| 目录 | 模块数 | 职责 |
|---|---:|---|
| `std/core/` | 53 | 类型、内存、位运算、数值与线性代数、单元与几何、运行时原语 |
| `std/data/` | 85 | 文档/媒体/系统镜像/字幕/网络配置等格式的解析与生成 |
| `std/encoding/` | 53 | 文本编码（base 家族、转义）、校验和、哈希、压缩、KDF |
| `std/net/` | 53 | 传输、应用协议、安全与策略、运维与可观测 |
| `std/text/` | 52 | 字符串、搜索、分析、排版辅助 |
| `std/container/` | 33 | 顺序/关联容器、图与树、位图、缓存 |
| `std/os/` | 32 | 文件、进程、线程、时间、终端、系统信息 |
| `std/async/` | 13 | 语言级 `async`/`await` 的运行时与横切面 |
| `std/analysis/` | 3 | 用 c6 写的 c6 静态分析（lex/parse/ub） |
| **合计** | **377** | |

---

## 2. 族覆盖矩阵

标记：**✅ 完整**＝该族的主干都已覆盖；**◐ 部分**＝只做了常用子集；**— 不做**＝
明确排除（理由见第 3 节）。

### 2.1 `std/encoding`（53）

| 族 | 代表模块 | 状态 |
|---|---|---|
| base 家族 | base32 / base45 / base58 / base64 / base85 / base91 / base16(hexdump) | ✅ |
| 转义与行编码 | uu / yenc / ebcdic / otp / punycode / varint / bitbuf / frame(COBS/SLIP) | ✅ |
| 校验和 | crc16 / crc32c / adler32 / fnv / luhn / hamming / classic | ✅ |
| 非加密哈希 | murmur3 / xxhash / siphash | ✅ |
| 加密哈希 | md5 / sha1 / sha256 / sha512 / sha3 / blake2s | ✅ |
| 对称与认证 | chacha20 / poly1305 / aes(核心轮函数) / otp | ◐ 无分组模式与 AEAD 组合层（刻意留给调用方） |
| KDF | hkdf / pbkdf2 | ✅ |
| 压缩 | rle / lz4 / snappy / deflate / zlib / gzip / lzw / huffman / bwt / mtf / delta | ✅ |
| 结构化序列化 | asn1 / pem(在 data) / varint | ✅ |
| base36 / base62 | — | — 变体，无新增能力 |

### 2.2 `std/data`（85）

| 族 | 代表模块 | 状态 |
|---|---|---|
| 结构化文本 | json / yaml / toml / xml / kdl / ini / props / dotenv / query / bencode | ✅ |
| 二进制序列化 | cbor / msgpack / ubjson / bson / plist / protobuf(手写编解码) | ✅ |
| 表格与清单 | csv / vcf / ics / crontab / hosts / resolvconf / sshconfig / netrc / knownhosts | ✅ |
| 文档与邮件 | html / markdown(在 text) / rtf / mbox / pem / x509 / keys / jwt / sqlite | ✅ |
| 音频容器 | wav / aiff / au / voc / ircam / flac / ogg / mp3 / midi | ✅ |
| 图像 | png / bmp / tga / qoi / pnm / gif / tiff / ico / pcx / hdr / xbm / xpm / svg / bdf / woff / ttf | ✅ |
| 系统与可执行 | elf / pe / macho / coff / class / wasm / rpm / ar / cpio / tar / zip / fat / ext2 / iso / mbr | ✅ |
| 字幕与播放列表 | srt / ass / lrc / m3u / vtt | ✅ |
| 几何与网格 | obj / stl / ply / mtl | ✅ |
| 地理与轨迹 | gpx / kml / geojson / maidenhead / geohash / dms | — **未做**（见第 3 节：与 xml/json 组合即可，收益低） |
| Office / Parquet / 7z | — | — 不做 |

### 2.3 `std/net`（53）

| 族 | 代表模块 | 状态 |
|---|---|---|
| 传输与地址 | tcp / udp / uds / inet / cidr | ✅ |
| 名字解析 | dns / resolver | ◐ 不做 DNSSEC 验签 |
| 应用协议（客户端） | httpc / ws / sse / socks5 / mqtt / redis / postgres / mysql / ftp / smtp / irc / telnet / gopher / whois / ident / nntp / gemini / coap / ssdp / stun / tftp / ntp | ✅ |
| HTTP 服务端栈 | http / httpd / router / assets / session / multipart / cookie / httpcache / ratelimit | ✅ |
| 安全与策略 | tlskdf / tlsrec / https / httpsa / x25519 / hsts / robots | ◐ 记录层 + 密钥调度，无完整握手状态机 |
| 运维与可观测 | syslog / snmp / netflow / prometheus / wol | ✅ |
| QUIC / HTTP3 | — | — 不做 |
| LDAP / RADIUS / SIP | — | — 不做 |
| SNMP v3（USM/加密） | — | — 不做（v2c 已覆盖常见运维） |

### 2.4 `std/text`（52）

| 族 | 代表模块 | 状态 |
|---|---|---|
| 字符串与缓冲 | str / strbuf / fmt / num / casing / quote / sanitize / tabs / align | ✅ |
| Unicode | utf8 / utf16 / ascii / width / translit / collate | ✅ |
| 搜索与匹配 | kmp / bmh / aho / rabin / suffix / regex / fuzzy / glob / inverted / bm25 / keyword / stem | ✅ |
| 分析与度量 | words / sentences / readability / natural / ngram / phonetic / plural / hyphen | ✅ |
| 排版与标记 | wrap / table / markdown / rst / org / pod / tex / template / diff / ansi | ✅ |
| slug / roman / nato | ✅ 小工具 | ✅ |

### 2.5 `std/core`（53）

| 族 | 代表模块 | 状态 |
|---|---|---|
| 基础 | types / mem / err / bits / binio / bitio / endian / atomic / pool / rand | ✅ |
| 数值 | math / bignum / decimal / rational / complex / fixed / half / quantile / histogram / moving / series / least | ✅ |
| 线性代数与数值方法 | matrix / vector / lu / eigen / tridiag / sparse / fft / spline / polynomial / interp / solve / ode / optimize | ✅ |
| 几何 | geometry / geo / units / hull / polygon / segment / closest / color | ✅ |
| 运行时 | timer / task / future / executor / awaitgroup / retry / hash / crc | ✅ |

### 2.6 `std/container`（33）· `std/os`（32）· `std/async`（13）· `std/analysis`（3）

| 族 | 状态 |
|---|---|
| 顺序容器与适配器（vec/list/deque/ring/stack/queue/pqueue/heap/span） | ✅ |
| 关联容器（map/set/treap/skiplist/trie/lru/counter/bag/slots） | ✅ |
| 位与概率（bitset/bloom） | ✅ |
| 图与树算法（graph/mst/flow/scc/match/fenwick/segtree/dsu/interval/range/region） | ✅ |
| os：文件/进程/线程/时间/终端/信号/管道/系统信息 | ✅ |
| os：cgroup / netlink / udev | — 不做（Linux 专有且与命令行为界） |
| async：aio/achan/art/apump/co/await/scope/limiter/abench/abudget/agroup/apool/acancel | ✅ |
| analysis：lex/parse/ub | ✅ |

---

## 3. 明确「不做」清单（写清楚理由，避免以后反复讨论）

| 缺口 | 为什么不做 |
|---|---|
| QUIC / HTTP/3 | 需要完整 TLS 1.3 握手、丢包恢复与拥塞控制；体量与「判断型/编解码型」的标准库定位不符 |
| TLS 1.3 完整握手 | 现有 `tlskdf`/`tlsrec`/`https` 覆盖到「记录层 + 密钥调度」，握手状态机（含证书链校验策略）留给应用 |
| SNMP v3（USM/DES/AES） | v2c 已覆盖运维实际用量的绝大多数；v3 引入用户/密钥协商 |
| LDAP / RADIUS / SIP | 协议面很宽（ASN.1 结构 + 大量可选字段），且最常见的场景是「调外部服务」 |
| Office（xlsx/docx）/ Parquet / 7z | 需要巨型规范与大量压缩/加密分支 |
| GPX / KML / GeoJSON | 用 `std/data/xml` 与 `std/data/json` 组合几十行就能读，单列模块收益低 |
| base36 / base62 等 | 与 base58/base91 同族变体，没有新能力 |
| 词典级断词 / 排序 | 需要随包发布词典（体积 + 维护），现有启发式够用 |

---

## 4. 判定「到头」的三条标准

1. **族清单没有空缺**：上面每个「族」至少有一个模块，或明确列进第 3 节的不做清单；
2. **新模块只能是变体**：把它写出来以后，`std/` 的**能力集合**没有变化（只是多一种
   编码/协议方言）；
3. **不引入外部数据或大运行时**：不需要随包词典、不需要新的第三方依赖。

按这三条，当前状态是：**第 1 条成立，第 2 条成立**（本批之后我们评估过的候选项都
落在「变体」或「不做」里），因此**本轮收尾**。

## 5. 以后真要再加，怎么加

沿用已经跑通九十一批的节奏（每批 4 个模块）：

1. 选 4 个同主题、能写成方法 + 游标/句柄的模块；
2. 每个模块 2 个 `test` 块（真实规范向量 + 坏输入），API 一律「首参结构体指针 ⇒
   方法」，状态型模块补 `XxxDoc`（`open/next/count/reset`）；
3. 一个集成示例 `examples/std*.c6` + Makefile 目标（`STD_*_SRCS` 与 `.PHONY`、
   help、`STD_TARGETS` 两处同步 —— `test-all` 与 `check-all` 用的是同一份目标表）
   + err 号段（`tcc/tccgen.c` 与 `docs/reference/grammar.md` §8.1）+ 五处文档
   （`stdlib.md`、`history/status.md`、`README.md`、`CHANGELOG.md`、本文件的矩阵）；
4. 验收：`cmake --build build --target stdtest`、对应示例、`cmake --build build --target check-all`；
5. 提交并 ff 并入 main。

两条已经踩过、写进 `docs/stdlib.md` 的硬约束再强调一次：

- **跨模块同名的方法**（首参结构体指针）按接收者编名字，不会撞；
- **首参不是结构体指针的自由函数是全局符号**，两个模块同名会被静默合并——要么做成
  方法，要么名字带模块前缀。
