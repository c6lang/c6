# 标准库覆盖面盘点（`std/` 到底还缺什么）

这份文档回答一个问题：**「继续扩 `std/`，什么时候才算到头？」**

结论先给：到目前为止（**391 个模块、九十二批**、`cmake --build build --target stdtest`
391/391 全绿），**按族覆盖已经没有空缺**，连当初列进「不做」清单的两条大字头——
QUIC/HTTP/3 与 TLS 1.3 完整握手——也在第九十二批做掉了。剩下的缺口只剩一类：
**必须引入外部数据（词典）或第三方大运行时**的东西。所以本轮的收尾动作仍然是
把覆盖面写成这份可核对的矩阵，而不是继续堆数量。

---

## 1. 规模与分布

| 目录 | 模块数 | 职责 |
|---|---:|---|
| `std/core/` | 53 | 类型、内存、位运算、数值与线性代数、单元与几何、运行时原语 |
| `std/data/` | 85 | 文档/媒体/系统镜像/字幕/网络配置等格式的解析与生成 |
| `std/encoding/` | 54 | 文本编码（base 家族、转义）、校验和、哈希、压缩、KDF、AEAD |
| `std/net/` | 66 | 传输、应用协议、安全与策略、运维与可观测、QUIC/HTTP/3 |
| `std/text/` | 52 | 字符串、搜索、分析、排版辅助 |
| `std/container/` | 33 | 顺序/关联容器、图与树、位图、缓存 |
| `std/os/` | 32 | 文件、进程、线程、时间、终端、系统信息 |
| `std/async/` | 13 | 语言级 `async`/`await` 的运行时与横切面 |
| `std/analysis/` | 3 | 用 c6 写的 c6 静态分析（lex/parse/ub） |
| **合计** | **391** | |

---

## 2. 族覆盖矩阵

标记：**✅ 完整**＝该族的主干都已覆盖；**◐ 部分**＝只做了常用子集；**— 不做**＝
明确排除（理由见第 3 节）。

### 2.1 `std/encoding`（54）

| 族 | 代表模块 | 状态 |
|---|---|---|
| base 家族 | base32 / base45 / base58 / base64 / base85 / base91 / base16(hexdump) | ✅ |
| 转义与行编码 | uu / yenc / ebcdic / otp / punycode / varint / bitbuf / frame(COBS/SLIP) | ✅ |
| 校验和 | crc16 / crc32c / adler32 / fnv / luhn / hamming / classic | ✅ |
| 非加密哈希 | murmur3 / xxhash / siphash | ✅ |
| 加密哈希 | md5 / sha1 / sha256 / sha512 / sha3 / blake2s | ✅ |
| 对称与认证 | chacha20 / poly1305 / aes(核心轮函数) / otp / aead(AES-GCM + ChaCha20-Poly1305) | ✅（AEAD 组合层在第九十二批补上） |
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

### 2.3 `std/net`（66）

| 族 | 代表模块 | 状态 |
|---|---|---|
| 传输与地址 | tcp / udp / uds / inet / cidr | ✅ |
| 名字解析 | dns / resolver | ◐ 不做 DNSSEC 验签 |
| 应用协议（客户端） | httpc / ws / sse / socks5 / mqtt / redis / postgres / mysql / ftp / smtp / irc / telnet / gopher / whois / ident / nntp / gemini / coap / ssdp / stun / tftp / ntp | ✅ |
| HTTP 服务端栈 | http / httpd / router / assets / session / multipart / cookie / httpcache / ratelimit | ✅ |
| 安全与策略 | tlskdf / tlsrec / https / httpsa / x25519 / hsts / robots | ✅（`quictls` 补上 TLS 1.3 握手消息层与 QUIC 密钥表） |
| 运维与可观测 | syslog / snmp / netflow / prometheus / wol | ✅ |
| QUIC / HTTP/3 | quic / quichp / quicrecovery / quicstream / quicconn / quicd / h3conn / h3c / h3d | ✅（1-RTT + 头保护 + 丢包恢复 + QPACK + 客户端/服务端驱动） |
| HTTP/2 头压缩 | hpack | ✅（与 QPACK 共用一套静态表实现） |
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
| QUIC / HTTP/3 | **已做**（第九十二批，14 个模块）。仍然不做的是 0-RTT、连接迁移、多路径与地址验证令牌的服务端策略 |
| TLS 1.3 完整握手 | **已做**（`std/net/quictls`：ClientHello→Finished 的消息层、密钥调度与证书链校验）；不做的只是证书**策略**（信任锚、吊销） |
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

按这三条，当前状态是：**第 1 条成立**（`std/net` 里当初标「不做」的 QUIC/HTTP3 与
TLS 1.3 握手已经在第九十二批补上，只剩第 3 节里几条写明理由的例外），**第 2 条成立**，
**第 3 条成立**（那 14 个模块全部是零依赖的纯 c6 代码，最大的 `quictls` 也只用到
`std/encoding` 里的 sha256/hkdf/aead）。剩下能想到的候选项都落在「变体」或
「需要外部数据」里，因此**本轮收尾**。

## 5. 以后真要再加，怎么加

沿用已经跑通九十二批的节奏（每批 4 个模块；第九十二批是唯一的例外，
按「QUIC 全栈」这个整体一次做了 14 个）：

1. 选 4 个同主题、能写成方法 + 游标/句柄的模块；
2. 每个模块 2 个 `test` 块（真实规范向量 + 坏输入），API 一律「首参结构体指针 ⇒
   方法」，状态型模块补 `XxxDoc`（`open/next/count/reset`）；
3. 一个集成示例 `examples/std*.c6` + `tests/regression.sh` 的用例分支
   （用例名同时加进 `C6_TEST_ALL` / `C6_STD_TARGETS` 两处 —— `test-all` 与
   `check-all` 用的是同一份目标表）
   + err 号段（`tcc/tccgen.c` 与 `docs/reference/grammar.md` §8.1）+ 五处文档
   （`stdlib.md`、`history/status.md`、`README.md`、`CHANGELOG.md`、本文件的矩阵）；
4. 验收：`cmake --build build --target stdtest`、对应示例、`cmake --build build --target check-all`；
5. 提交并 ff 并入 main。

两条已经踩过、写进 `docs/stdlib.md` 的硬约束再强调一次：

- **跨模块同名的方法**（首参结构体指针）按接收者编名字，不会撞；
- **首参不是结构体指针的自由函数是全局符号**，两个模块同名会被静默合并——要么做成
  方法，要么名字带模块前缀。
