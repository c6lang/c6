# 变更史

> **状态**：现行 · **索引**：[文档索引](docs/README.md) · **实施状态**：[docs/history/status.md](docs/history/status.md)
>
> 这里是**按特性归档的变更史**（不是逐条 commit 日志）。每条给一个代表性提交的短
> hash，完整历史用 `git log` 查。日期取该特性的收尾提交。

## 未发布

### 文档

- **补齐第十一批的号段与状态**：`docs/reference/grammar.md` §8.1 的 std 号段表
  补上 `std.core.crc`=0x6035…`std.encoding.frame`=0x6038（第十一批漏更新），
  `docs/history/status.md` 补写第十一批小节；第十二批（regex/diff/fuzzy/wrap）
  的号段与状态一并写入。

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

- **裸机引导器：payload 直接用 ATA PIO 读进 0x400000**（`examples/boot/boot.S`）。
  原先实模式用 INT 13h 把 payload 读到 0x10000 再搬过去，目标地址受「段:偏移」
  约 1 MiB 的限制；payload 超过 ~576 KiB 时还会压到 0xA0000 起的 VGA/ROM 空洞
  （写进去读出来是 0xFF）。`tests/data_zero.c6`（855 KiB payload）因此在裸机上
  3/4 失败。现在进 32 位保护模式后用 ATA PIO（主通道、28 位 LBA、轮询）一次
  一个扇区直接写进链接地址 0x400000，不再需要中转和搬运；`make fstest-bare`,
  `fstest-bare-tests`（38 通过 0 失败）、`fstest-bare-go`、`fstest-bare-handle`、
  `fstest-bare-smp` 全过，`make check-all` 绿。

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

- **标准库第四十二批（老式文本编码）**：`std.encoding.base91`（basE91）、
  `std.encoding.uu`（uuencode/uudecode）、`std.encoding.yenc`（yEnc 含 `=ybegin`
  头部字段）、`std.encoding.ebcdic`（CP037 双向表），集成示例
  `examples/stdtranscode.c6`（`make std-transcode`）。错误号段
  `std.encoding.base91`=0x60b1 … `std.encoding.ebcdic`=0x60b4
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十一批（分区与文件系统）**：`std.data.mbr`（MBR + GPT）、
  `std.data.fat`（FAT12/16/32 BPB 与目录项）、`std.data.iso`（ISO 9660 主卷与
  目录记录）、`std.data.ext2`（超级块 + 块组描述符 + inode），集成示例
  `examples/stddisk.c6`（`make std-disk`）。错误号段 `std.data.mbr`=0x60ad …
  `std.data.ext2`=0x60b0（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第四十批（字体与矢量）**：`std.data.bdf`（BDF 位图字体）、
  `std.data.woff`（WOFF 1.0 容器）、`std.data.svg`（路径规范化 + 包围盒）、
  `std.data.ttf`（表目录 + cmap/hmtx 度量），集成示例 `examples/stdfont.c6`
  （`make std-font`，链接带 `-lm`）。错误号段 `std.data.bdf`=0x60a9 …
  `std.data.ttf`=0x60ac（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十九批（老图形格式）**：`std.data.pcx`（RLE + 调色板/24 位/1 位）、
  `std.data.hdr`（Radiance RGBE，新老两种扫描线，含 `hdr_to_rgb`）、
  `std.data.xbm`（C 源码位图）、`std.data.xpm`（C 源码彩图 + 透明键），集成示例
  `examples/stdlegacy.c6`（`make std-legacy`）。错误号段 `std.data.pcx`=0x60a5 …
  `std.data.xpm`=0x60a8（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十八批（哈希与校验）**：`std.encoding.sha3`（SHA3-224/256/384/512 +
  SHAKE128/256）、`std.encoding.blake2s`（输出 1~32 字节）、`std.encoding.crc32c`
  （CRC-32C，含分段接口）、`std.encoding.xxhash`（XXH32/XXH64，一次性 + 流式），
  集成示例 `examples/stdhash.c6`（`make std-hash`）。错误号段
  `std.encoding.sha3`=0x60a1 … `std.encoding.xxhash`=0x60a4（`c6_err_std_slots[]`
  追加，`grammar.md` §8.1 同步）。

- **标准库第三十七批（系统视图）**：`std.os.sysinfo` / `std.os.resource` /
  `std.os.user` / `std.os.pty` 四个 libc 薄封装模块，集成示例
  `examples/stdsys.c6`（`make std-sys`）。sysinfo 给 uname/运行时长/负载/CPU 数/
  页大小；resource 给 rlimit 与 getrusage（毫秒 CPU 时间、峰值 RSS、缺页、上下文
  切换）；user 给 passwd/group 查询（查不到 0、出错 -1）；pty 给伪终端一对 +
  fork 子进程 + 带超时的收发 + 窗口大小。错误号段 `std.os.sysinfo`=0x609d …
  `std.os.pty`=0x60a0（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十六批（序列化与标记）**：`std.data.bson` / `std.data.ubjson` /
  `std.data.plist` / `std.text.markdown` 四个零分配模块，集成示例
  `examples/stdser.c6`（`make std-ser`）。BSON 逐元素切视图并做 int32 符号扩展；
  UBJSON 走事件流 + 容器栈，支持 `$`/`#` 优化容器；plist 解 `bplist00` 的偏移表
  与引用号（支持 null/bool/int/real/date/data/ASCII/UTF-16/array/dict/uid）；
  markdown 把常用子集渲染成 HTML（标题/段落/围栏代码/引用/列表/分隔线 +
  行内代码、粗斜体、链接、自动链接）。错误号段 `std.data.bson`=0x6099 …
  `std.text.markdown`=0x609c（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十五批（二进制容器格式）**：`std.data.cpio` / `std.data.pe` /
  `std.data.macho` / `std.data.wasm` 四个零分配结构解析模块，集成示例
  `examples/stdexec.c6`（`make std-exec`）。cpio 认 newc/crc；pe 走 DOS 头到节表
  并做 RVA→文件偏移映射（PE32/PE32+ 分开处理）；macho 认 thin 32/64 位与 fat
  外壳，抓 `LC_UUID`/`LC_MAIN`；wasm 逐节遍历并解导出表。错误号段
  `std.data.cpio`=0x6095 … `std.data.wasm`=0x6098（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第三十四批（网络协议编解码）**：`std.net.sse` / `std.net.socks5` /
  `std.net.mqtt` / `std.encoding.snappy` 四个零分配字节层编解码模块，集成示例
  `examples/stdproto.c6`（`make std-proto`）。sse 按空行派发事件、多行 data 拼进
  调用方缓冲；socks5 构建问候/认证/请求并解析应答（IPv4/IPv6/域名）；
  mqtt 处理 3.1.1 的固定头与变长剩余长度、CONNECT/PUBLISH/SUBSCRIBE 与主题
  通配；snappy 实现块格式编解码（重叠复制、哈希表贪心匹配）。错误号段
  `std.net.sse`=0x6091 … `std.encoding.snappy`=0x6094（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第三十三批（结构化文本格式）**：`std.data.html` / `std.data.yaml` /
  `std.data.vcf` / `std.data.ics` 四个零分配读取器，集成示例
  `examples/stddoc.c6`（`make std-doc`）。html 是六种 token 的分词器（属性按需
  扫、`script` 走原文、实体解码）；yaml 是扁平事件 + `depth`，支持注释与引号
  但不管流式与块标量；vcf/ics 都是「先摊平折行，再逐行读」。错误号段
  `std.data.html`=0x608d … `std.data.ics`=0x6090（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第三十二批（图像与 LZW）**：`std.encoding.lzw` / `std.data.gif` /
  `std.data.tiff` / `std.data.ico` 四个零分配模块，集成示例
  `examples/stdgfx.c6`（`make std-gfx`）。lzw 是 GIF 变体（LSB 打包、clear/eoi、
  码宽 2..12）；gif 支持全局/局部调色板、透明色、隔行扫描与多帧；tiff 支持
  II/MM 字节序、STRIP、8 位、none/Deflate(zlib)/PackBits 压缩与 RGB/灰度；
  ico 支持 DIB（1/4/8/24/32 位）与内嵌 PNG 负载。顺带给 `std.data.png` 补了
  像素解码 `png_decode`（颜色类型 0/2/3/4/6、五种滤波、tRNS）。错误号段
  `std.encoding.lzw`=0x6089 … `std.data.ico`=0x608c（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第三十一批（编码与凭据）**：`std.encoding.asn1` / `std.data.x509` /
  `std.data.jwt` / `std.encoding.otp` 四个零分配模块，集成示例
  `examples/stdenc.c6`（`make std-enc`）。asn1 是 DER 读取器（多字节 tag、长
  长度、INTEGER/OID/BIT STRING/时间/字符串解码）；x509 在它上面解析 v3 证书
  （版本/序列号/签名 OID/issuer·subject 的 CN·O·C/有效期/公钥位数/签名视图，
  不验签）；jwt 验 HS256/HS384/HS512 紧凑令牌（常量时间比较，`alg=none` 与非
  HMAC 拒绝）；otp 给 RFC 4226 的 HOTP 与 RFC 6238 的 TOTP（带时间窗口校验）。
  `std.encoding.sha512` 顺带补 `hmac_sha384`。错误号段 `std.encoding.asn1`=0x6085
  … `std.encoding.otp`=0x6088（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第三十批（图像解码）**：`std.data.bmp` / `std.data.tga` /
  `std.data.qoi` / `std.data.pnm` 四个模块，集成示例 `examples/stdimg.c6`
  （`make std-img`）。四个解码器都输出 RGBA8888、零分配：bmp 支持 24/32 位
  真彩与 8 位调色板、上下行序与 4 字节行对齐；tga 支持类型 2/10（未压缩/
  RLE）与四种原点；qoi 既能解码全套操作码（INDEX/DIFF/LUMA/RUN/RGB/RGBA）
  也能编码（RUN/INDEX/RGB/RGBA）；pnm 支持 P5/P6、注释、maxval 1..65535
  与 8/16 位大端样本缩放。错误号段 `std.data.bmp`=0x6081 … `std.data.pnm`=0x6084
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十九批（二进制文件格式）**：`std.data.wav` / `std.data.png` /
  `std.data.elf` / `std.data.zip` 四个只读解析模块，集成示例
  `examples/stdbin.c6`（`make std-bin`）。wav 解析 RIFF/WAVE 的 `fmt `/`data`
  块（跳过未知块、奇数补齐、fmt 必须在 data 前）；png 遍历签名 + 块并校验
  IEEE CRC-32（`png_parse` 直接给 IHDR 的宽/高/位深/颜色类型）；elf 支持
  ELF32/64 与小端/大端，读头 + 节表并从 `shstrndx` 字符串表取节名；zip 从
  EOCD 找中央目录、按本地头取数据，stored 直接拷贝、deflate 走
  `std.encoding.deflate`，并校验 CRC-32 与大小。四个模块零分配、只读不拷贝。
  错误号段 `std.data.wav`=0x607d … `std.data.zip`=0x6080（`c6_err_std_slots[]`
  追加，`grammar.md` §8.1 同步）。

- **标准库第二十八批（压缩与归档）**：`std.encoding.deflate` /
  `std.encoding.zlib` / `std.encoding.gzip` / `std.encoding.tar` 四个模块，
  集成示例 `examples/stdarch.c6`（`make std-arch`）。deflate 的 `inflate` 完整
  支持 stored/固定 Huffman/动态 Huffman 三种块（回溯直接读输出缓冲），压缩侧
  给 `deflate_stored` 与「贪心 LZ77 + 固定 Huffman」的 `deflate_fixed`；zlib
  是 `78 01` 头 + DEFLATE + Adler-32（`adler32`/`adler32_update`）；gzip 是
  10 字节头 + DEFLATE + CRC-32/ISIZE（`gzip_crc32` 走 `std.core.crc`，
  `gzip_decompress` 会跳过 FEXTRA/FNAME/FCOMMENT/FHCRC 并校验页脚）；tar 是
  ustar 的读写（checksum、prefix/name 拆分、512 对齐、结尾两个零块）。四个
  模块都是零分配、调用方给缓冲。错误号段 `std.encoding.deflate`=0x6079 …
  `std.encoding.tar`=0x607c（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十七批（数值工具）**：`std.core.geometry` / `std.core.interp` /
  `std.core.stats` / `std.core.matrix` 四个模块，集成示例 `examples/stdcalc.c6`
  （`make std-calc`）。geometry 是 Vec2/Vec3/Rect（自由函数、纯值、牛顿迭代
  开方不链 libm）；interp 是 lerp/inv_lerp/remap/smoothstep 与 11 条缓动曲线
  （端点严格 0→1，sin 用 15 阶泰勒）加一维 Catmull-Rom；stats 是 Welford 流式
  均值/方差/最值，加中位数/分位数（就地排序 + 线性插值）/直方图/Pearson 相关；
  matrix 是行主序矩阵（`mat_strided` 支持子矩阵视图）与由调用方给 scratch 的
  行列式、线性方程组。四个模块都是纯函数、零分配、自成一体。错误号段
  `std.core.stats`=0x6075 … `std.core.interp`=0x6078（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第二十六批（数据格式）**：`std.data.bencode` / `std.data.msgpack` /
  `std.data.cbor` / `std.data.pem` 四个模块，集成示例 `examples/stddata.c6`
  （`make std-data`）。bencode 是 BitTorrent 的元信息格式（写整数/字节串/
  列表/字典，读端给标记流，拒绝前导零与溢出）；msgpack 与 cbor 是两种紧凑
  二进制（写端覆盖 nil/bool/整数/浮点/字符串/二进制/数组/映射，读端给
  `MpTag`/`CborTag`；msgpack 拒绝扩展类型与保留码，cbor 拒绝不定长、标签与
  `f7`/`f8`，半/单精度都转 f64）；pem 是 RFC 7468 的 BEGIN/END + Base64 封装
  （`pem_encode` 每行 64 列、`pem_reader` 多块迭代、`decode_der` 走
  `std.encoding.base64`）。四个模块都是纯函数、零分配、调用方给缓冲。错误号段
  `std.data.msgpack`=0x6071 … `std.data.pem`=0x6074（`c6_err_std_slots[]` 追加，
  `grammar.md` §8.1 同步）。

- **标准库第二十五批（对称加密基础）**：`std.encoding.aes` /
  `std.encoding.chacha20` / `std.encoding.poly1305` / `std.encoding.random`
  四个模块，集成示例 `examples/stdcipher.c6`（`make std-cipher`）。aes 是
  FIPS 197 的分组密码（`aes_key` 支持 128/192/256 位、10/12/14 轮，
  `encrypt_block`/`decrypt_block`/`ctr_xor`，S 盒与逆 S 盒由 GF(2^8) 生成）；
  chacha20 是 RFC 8439 的流密码（`chacha20_block`/`chacha20_xor`，12 字节
  nonce、32 位计数器）；poly1305 是一次性 MAC（5×26 位肢体做 130 位模运算、
  u64 乘积、无 128 位整数）；random 从 `/dev/urandom` 取安全随机
  （`rand_bytes`/`rand_u64`/`rand_below` 用拒绝采样避免取模偏置/`rand_hex`）。
  三者都只给原语：CTR/流密码未认证，组合方式（encrypt-then-MAC）写在文档与
  示例里。错误号段 `std.encoding.aes`=0x606d … `std.encoding.random`=0x6070
  （`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十四批（文件系统进阶）**：`std.os.walk` / `std.os.temp` /
  `std.os.lock` / `std.os.mmap` 四个模块，集成示例 `examples/stdfsx.c6`
  （`make std-fsx`）。walk 把 `std.os.fs` 的单层 `Dir` 包成整棵树
  （`walk`/`walk_stat` 带深度/符号链接/目录回调选项与文件/目录/字节计数，
  回调返回 0 跳过子树，`remove_tree` 后序删除）；temp 是 `mkstemp`/`mkdtemp`
  的安全封装（`temp_file`/`temp_file_named`/`temp_dir`，0600 + `FD_CLOEXEC`，
  不自动删除）；lock 是 `fcntl` 建议锁（`acquire` 的阻塞/非阻塞/超时三态、
  同一进程两个句柄不冲突、跨进程才互斥）；mmap 做文件与匿名映射
  （`map_file` 可写走 `MAP_SHARED`、`map_anon` 清零、`sync`/`read_at`/`write_at`
  带边界检查、`unmap` 可重复）。错误号段 `std.os.walk`=0x6069 …
  `std.os.mmap`=0x606c（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十三批（消息摘要与密钥派生）**：`std.encoding.md5` /
  `std.encoding.sha512` / `std.encoding.hkdf` / `std.encoding.pbkdf2` 四个模块，
  集成示例 `examples/stdcrypto.c6`（`make std-crypto`）。md5 是兼容用的校验和
  （`Md5` 的 `init`/`update`/`final`、`md5`/`md5_hex`/`md5_eq`，RFC 1321 的 T 表，
  明确标注不安全）；sha512 补 SHA-512/SHA-384 与 HMAC-SHA-512（128 位长度计数、
  `init384` 换 IV、`hmac_sha512` 的 128 字节块）；hkdf 在 HMAC-SHA-256 上做
  `extract`/`expand`（RFC 5869，空 salt 按 32 个零字节、expand 上限 8160 字节）；
  pbkdf2 是带迭代的口令 KDF（`pbkdf2_sha1`/`pbkdf2_sha256`，RFC 6070/8018，
  `iters <= 0` 直接拒绝）。错误号段 `std.encoding.md5`=0x6065 …
  `std.encoding.pbkdf2`=0x6068（`c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。

- **标准库第二十二批（Web 服务端）**：`std.net.httpd` / `std.net.router` /
  `std.net.assets` / `std.net.session` 四个模块，集成示例
  `examples/stdserve.c6`（`make std-serve`）。httpd 把 `std.net.http` 的解析和
  `std.net.tcp` 的收发接成服务端骨架（`httpd_listen`/`accept_conn`、`Conn` 的
  `read_request` 三态与 `begin`/`send_body`/`respond`/`respond_text`、
  `Server.serve_once` 的处理器回调；一次连接一个请求，chunked 请求直接 400）；
  router 是固定 32 条的路由表（`:name` 段参数、`*`/`*name` 尾段通配、方法
  大小写不敏感、`"*"` 任意方法、先注册先赢、按 `/` 严格切段）；assets 把一个
  root 目录当静态资源发（百分号解码后拒绝 `..`/NUL、归一后必须仍在 root 下、
  默认 `lstat` 拒符号链接、8 KiB 流式写出、Content-Type 走 `std.net.mime`）；
  session 是 16 槽的内存会话表（`create`/`find`/`destroy`/`expire`、
  `key=value\n` 数据行、`sid` cookie、id 来自 `std.core.rand` 且明确非密码学）。
  错误号段 `std.net.httpd`=0x6061 … `std.net.session`=0x6064（`c6_err_std_slots[]`
  追加，`grammar.md` §8.1 同步）。

- **标准库第二十一批（网络客户端）**：`std.net.udp` / `std.net.uds` /
  `std.net.resolver` / `std.net.httpc` 四个模块，集成示例
  `examples/stdclient.c6`（`make std-client`）。udp 是数据报薄封装
  （`udp_bind`/`udp_connect`、`send_to`/`send_bytes`/`recv_bytes`/`recv_from`、
  `set_timeout`/`set_broadcast`，超时 -1、空报文 0、缓冲区小就截断）；uds 是
  AF_UNIX 本地套接字（`uds_pair`、`uds_listen`/`uds_connect`/`accept_client`、
  `shutdown_write` 之后对端读到 EOF=0，路径 socket 的 `unlink` 交给调用方）；
  resolver 把 `std.net.dns` 的报文层和 `std.net.udp` 的传输层接起来
  （`resolver_open` 读 `/etc/resolv.conf`、`resolver_from_server`、`resolve_a`/
  `resolve_aaaa`/`resolve_ip`、`parse_reply` 校验 id/TC/rcode 并提取记录，
  `resolver_conf_parse`/`resolver_conf_file` 单独可测，`ipv4_format`/
  `ipv6_format`）；httpc 在 `std.net.tcp`+`std.net.url` 上做 HTTP/1.1 客户端
  （`build_request` 先量再写、`fetch` 一次到位、`parse_response` 的 1/0/-1 三态、
  chunked 解码含扩展与 trailer、`Response.header`/`header_int` 大小写不敏感，
  只做明文 `http://`）。错误号段 `std.net.udp`=0x605d … `std.net.httpc`=0x6060
  （`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加，`grammar.md` §8.1 同步）。
  另在 `std/core/err.c6` 里加 `#undef NO_DATA`：glibc 的 `<netdb.h>` 把这个
  名字定义成宏，会污染 `enum Error` 的成员名（import 是在导入方的宏环境里展开
  的）。

- **标准库第二十批（终端与进程控制）**：`std.os.tty` / `std.os.signal` /
  `std.os.pipe` / `std.os.poll` 四个模块，集成示例 `examples/stdterm.c6`
  （`make std-term`）。tty 是 termios 薄封装（`is_tty`、`size`/`width`/`height`、
  `TtyMode` 的 `raw`/`restore`/`is_active`，失败不动输出参数）；signal 包
  `sigaction`/`sigprocmask`/`raise`/`kill`（`install` 带 `SA_RESTART`、`ignore`/
  `reset`、`block`/`unblock`/`is_blocked`/`is_pending`、`send_self`/`send_to`、
  `signal_name`/`signal_number`，名字统一成动词以避开 libc 同名函数）；pipe 是
  匿名管道的 `Pipe`（`init`/`init_nonblock`、`read_bytes` 的 0=EOF、`read_exact`、
  `write_bytes` 返回实际字节数、`close_read`/`close_write`/`close_all`、
  `would_block()`，读写都重试 `EINTR`、fd 带 `FD_CLOEXEC`）；poll 把 `poll(2)`
  包成 32 槽零分配的 `PollSet`（`add` 同 fd 改事件、`wait` 返回就绪个数、
  `is_ready`/`ready`/`fd_at`）与单 fd 的 `wait_readable`/`wait_writable`。
  错误号段 `std.os.tty`=0x6059 … `std.os.poll`=0x605c（`c6_err_std_slots[]` 追加）。

- **标准库第十九批（并发与同步）**：`std.core.atomic` / `std.os.sync` /
  `std.os.thread` / `std.container.chan` 四个模块，集成示例
  `examples/stdthread.c6`（`make std-thread`）。atomic 是 `Atomic`/`Atomic32`
  的原子读写与读改写（默认 seq_cst，另有 `load_relaxed`/`store_release`
  发布口）、屏障与 TAS 自旋锁 `SpinLock`，走编译器的 `__atomic_*` 内建、
  不依赖 pthread；sync 是 pthread 薄封装的值类型 `Mutex`/`Cond`/`RWLock`/
  `Once`/`Semaphore`/`Barrier`（`init`/`destroy` 方法、`try_*`、限时等待）；
  thread 是线程身份/`cpu_count`/让步/改名与固定 worker 的 `ThreadPool`
  （任务队列由调用方给，`submit` 满时背压、`wait` 等空闲、`stop` join）；
  chan 是建在 Mutex/Cond 上的有界通道（元素按字节块拷贝，阻塞收发 +
  `try_*`/限时，`close_chan` 收尾）。错误号段 `std.core.atomic`=0x6055 …
  `std.container.chan`=0x6058（`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库第十八批（算法与容器）**：`std.container.graph` / `std.container.lru` /
  `std.container.trie` / `std.container.region` 四个模块，集成示例
  `examples/stdalgo.c6`（`make std-algo`）。graph 是前向星表示的图算法（BFS 无权
  最短路、迭代 DFS、Kahn 拓扑排序、并查集连通分量、非负权 Dijkstra + 前驱链）；
  lru 是链式哈希 + 双向链表的 O(1) 缓存（命中/未命中/淘汰统计）；trie 是任意
  字节键的前缀树（节点池由调用方给，池满不写坏结构）；region 是自动合并的
  半开区间集合（包含/重叠/找空隙，带溢出保护）。四个模块都零分配，错误号段
  `std.container.graph`=0x6051 … `std.container.region`=0x6054（`tcc/tccgen.c` 的
  `c6_err_std_slots[]` 追加）。

- **标准库第十七批（网络协议基础）**：`std.encoding.sha1` / `std.net.ws` /
  `std.net.dns` / `std.net.tcp` 四个模块，集成示例 `examples/stdwire.c6`
  （`make std-wire`）。sha1 是流式 + 一次性的 SHA-1 / HMAC-SHA1（含 RFC 2202
  与官方 100 万字节向量，文档写明只用于协议互操作）；ws 是 RFC 6455 的握手
  accept-key 与帧编解码（7/16/64 位长度、客户端掩码、区分「还不够」与「坏帧」）；
  dns 是 RFC 1035 的报文解析/查询构建（压缩指针防环、A/AAAA/CNAME、`*next`
  遍历）；tcp 是 POSIX 套接字薄封装（getaddrinfo、内核挑端口、poll 超时连接、
  收发/超时/半关闭，方法名避开 libc 的 send/recv/close/accept）。错误号段
  `std.encoding.sha1`=0x604d … `std.net.tcp`=0x6050（`tcc/tccgen.c` 的
  `c6_err_std_slots[]` 追加）。

- **标准库第十六批（命令行工具基础）**：`std.os.args` / `std.text.table` /
  `std.text.natural` / `std.os.proc` 四个模块，集成示例 `examples/stdcli.c6`
  （`make std-cli`）。args 是按规格表迭代的选项解析（长/短名、带值/开关、
  `--name=value`、`--` 终止、位置参数任意穿插，未知选项/缺值报错）；table 是按
  显示列宽对齐的表格（CJK 2 列、边框/竖线/内边距可关、左/右/居中）；natural 是
  自然排序（`file2 < file10`）与人类可读化（KiB/MiB、`1h 2m 3s`、千分位，且能
  反向解析）；proc 是 POSIX 子进程封装（fork+execvp+waitpid、退出码/信号、
  `/bin/sh -c`、PATH 查找）。错误号段 `std.os.args`=0x6049 … `std.os.proc`=0x604c
  （`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库第十五批（配置与文档格式）**：`std.data.toml` / `std.data.xml` /
  `std.data.query` / `std.text.template` 四个模块，集成示例
  `examples/stdconf.c6`（`make std-conf`）。toml 是事件式的 TOML 子集读取器
  （表/表数组、注释、基本与字面量字符串、十进制/0x/0o/0b 整数、浮点、布尔、
  跨行数组；语法错误给行号且能继续读）；xml 是零分配扫描器（START/END/SELF/TEXT、
  按需解析属性、CDATA 当文本、跳过注释/PI/DOCTYPE、32 层标签配对校验、实体解码）；
  query 是 form-urlencoded 的解析与构建（`+` 表空格、只按第一个 `=` 切、unreserved
  编码）；template 是 `{{name}}` 模板（`{{{{` 转义、缺变量严格/宽松两种模式、
  去重收集变量名）。错误号段 `std.data.toml`=0x6045 … `std.text.template`=0x6048
  （`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

- **标准库第十四批（压缩与文本编码）**：`std.encoding.lz4` / `std.encoding.rle` /
  `std.encoding.base85` / `std.encoding.hexdump` 四个模块，集成示例
  `examples/stdpack.c6`（`make std-pack`）。lz4 是标准 LZ4 块格式的零分配实现
  （哈希表与输出缓冲都由调用方给；手写标准向量 + 独立参考实现双向交叉验证）；
  rle 是 PackBits 游程与一阶差分（模 256，缓变采样差分后压到 1/40）；
  base85 是 Ascii85（Adobe，`z` 简写/部分组/`~>`）与 Z85（ZeroMQ，长度约束）
  两套字母表，与 Python `base64.a85encode` 做了随机交叉验证；hexdump 按固定
  布局排版并能反向解析（只把「8 位十六进制 + 空白」当偏移）。错误号段
  `std.encoding.lz4`=0x6041 … `std.encoding.hexdump`=0x6044（`tcc/tccgen.c` 的
  `c6_err_std_slots[]` 追加）。

- **标准库第十三批（数值类型）**：`std.core.bignum` / `std.core.decimal` /
  `std.core.rational` / `std.core.complex` 四个模块，集成示例
  `examples/stdnum.c6`（`make std-num`）。bignum 是 1024 位的值类型大整数
  （加减乘、二进制长除法、u32 快速幂、2..36 进制互转，超限/除零/解析失败走
  `err`）；decimal 是 9 位小数的十进制定点，**乘除用自带的 128 位小工具**
  （tcc 没有 `__int128`）再按 half-up 收尾，`0.1 + 0.2` 精确等于 `0.3`，带 5
  种舍入模式与「恰好 N 位小数」的格式化，并与 Python `decimal` 做了随机交叉
  验证；rational 是始终最简的精确分数，`cmp` 用连分数比较（不经过 double）；
  complex 是 f64 复数（四则/共轭/模/归一化/整数幂，除零按 IEEE）。错误号段
  `std.core.bignum`=0x603d … `std.core.complex`=0x6040（`tcc/tccgen.c` 的
  `c6_err_std_slots[]` 追加）。

- **标准库第十二批（文本算法）**：`std.text.regex` / `std.text.diff` /
  `std.text.fuzzy` / `std.text.wrap` 四个模块，集成示例
  `examples/stdtext.c6`（`make std-text`）。regex 是回溯式正则：UTF-8 按码点
  匹配、位置给字节，捕获组/分支/字符类/`{m,n}` 与懒惰量词都有，单字符量词和
  「定长链 + 一层捕获组」走迭代快路径（`.*` 在 200KB 文本上是线性的），固定
  容量在编译期报错、匹配深度 4096 防爆栈；diff 是 Hirschberg 线性空间 LCS +
  `diff -u` 同形 hunk（先剥公共前后缀，工作区走 `Arena`，放不下不写半截）；
  fuzzy 是编辑距离/OSA/千分比相似度/子序列打分与 `fuzzy_best`（按码点，
  `FUZZY_MAX`=256）；wrap 按显示列折行（CJK 2 列、CJK 之间可断行、超长词硬切）
  并带整段缩进与居中。错误号段 `std.text.regex`=0x6039 … `std.text.wrap`=0x603c
  （`tcc/tccgen.c` 的 `c6_err_std_slots[]` 追加）。

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
