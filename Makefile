# c6 —— 带语言扩展的 TinyCC
#
#   make bootstrap   准备构建（tcc/ 源码已在仓库里；缺 config.mak 时跑 configure）
#   make build       以扩展模式构建（EXTRA-DEFS=-DC6_EXT）
#   make build-plain 以上游模式构建（不定义 C6_EXT）
#   make selftest    跑 tests/ 下所有 c6 自测（test 块 + 编译期诊断）
#   make test        跑 tests2 上游全量测试（扩展模式）
#   make test-plain  关闭扩展构建并跑同一套上游测试
#   make test-all    selftest + tests2 全量 + c6selfcheck + freestanding/裸机
#   make check-all   test-all + C99 后端用例 + 三条自举路线 + 标准库（发布前跑这一条）
#   make c99         C99 输出后端已内置在 tcc/c6（-c99 / -o *.c / -o <目录>）
#   make c99test     与 selftest 相同的用例走 C99 后端（生成 C，再用 gcc 编译运行）
#   make c99tcc-test 同一批用例，但生成 C 的宿主编译器换成 tcc 自己（不依赖 gcc）
#   make c6selfcheck tcc 自己的源码拷成 .c6 后用 c6 规则编译（初始化规则 + C99 后端）
#   make selfhost    用 tcc 自举自己（stage1 -> stage2，比对目标文件）
#   make c99selfhost 用 C99 后端自举自己（生成 C -> gcc -> stage2 -> stage3）
#   make c99selfhost-tcc 同上，但生成 C 之后由 tcc 自己编译（整条链不需要 gcc）
#   make wasmrt      编目标平台的运行库 tcc/libtcc1-<arch>.a（需要 clang）
#   make wasmtest    tcc --target=<triple> 出 .wasm 再用 node 跑（见 docs/reference/llvm-backend.md）
#   make clean       清理 tcc/ 的构建产物和自举产物
#   make distclean   清理 tcc/ 的构建产物和 configure 结果（不删源码）
#
# C6_EXT 是编译期开关，切换模式时会自动先清理旧产物。

TCC_DIR  := tcc
SELFTEST := tests/run-tests.sh
ERRBASE := tests/run-errbase.sh
JOBS     ?= $(shell nproc 2>/dev/null || echo 4)
CFLAGS   ?= -g -O0 -Wall -Wno-unused-result
EXT      := EXTRA-DEFS=-DC6_EXT
MODE     := $(TCC_DIR)/.c6-mode

# 自举产物都放在这里：每条自举路线一个子目录，互不覆盖
SELFHOST     := build/selfhost
SELFHOST_C6  := $(SELFHOST)/c6
SELFHOST_C99 := $(SELFHOST)/c99
SELFHOST_C99TCC := $(SELFHOST)/c99tcc
# 自举时编译 tcc 自己用的定义：ONE_SOURCE 把整个编译器塞进一个编译单元，
# C6_EXT 打开 c6 扩展（与 make build 的 EXTRA-DEFS 一致）
SELFHOST_DEFS := -DONE_SOURCE=1 -DC6_EXT
# c6selfcheck 也要带 C6_EXT：源码里 #ifdef C6_EXT 的那一半（扩展的实现）
# 只有在打开扩展时才会被编到，不带它这项检查就只能覆盖编译器的一半。
SELFCHECK_DEFS := -DC6_EXT
SELFHOST_LIBS := -lm -ldl -lpthread
HOSTCC   ?= gcc

.PHONY: all bootstrap build build-plain test test-plain test-all check-all \
        selftest clean distclean help check-tcc mode-ext mode-plain c99 c99test \
        c99tcc-test c6selfcheck selfhost c99selfhost c99selfhost-tcc \
        stdtest errcheck std-demo std-tour std-kit std-box std-web std-net \
        std-more std-next std-node std-form std-embed std-text std-num std-pack std-conf std-cli std-wire std-algo std-thread std-term std-client std-serve std-crypto std-fsx std-cipher std-data std-calc std-arch std-bin std-img std-enc std-gfx std-doc std-proto std-exec std-ser std-sys std-hash std-legacy std-font std-disk std-transcode std-audio std-proto2 std-ds std-words std-math2 std-time std-packet std-db std-hash2 std-ordered std-bwt std-subs std-creds std-geo std-pkg std-check std-linux std-bin2 std-clean std-color std-cont std-stats std-search std-audio2 std-proto3 std-markup std-textan std-cfg2 std-match std-async std-async2 std-math3 std-linalg std-search2 std-graph2

all: build

# tcc/ 是本仓库的源码（上游基线 + 一个 c6 补丁一个提交），不再克隆和打补丁；
# bootstrap 只负责首次 configure，已经配置好就直接返回。
bootstrap:
	@./scripts/bootstrap.sh

check-tcc:
	@test -f $(TCC_DIR)/tccpp.c || { echo "缺少 $(TCC_DIR)/ 源码，检出可能不完整"; exit 1; }

# 记录当前构建模式，模式变化时清理，避免复用错误配置的目标文件。
# bootstrap 先跑：首次构建前把 tcc/ 配置好，再决定要不要清理。
mode-ext: bootstrap
	@if [ "$$(cat $(MODE) 2>/dev/null)" != "ext" ]; then \
	    echo "c6: 切换到扩展模式（清理旧产物）"; \
	    $(MAKE) -C $(TCC_DIR) clean >/dev/null 2>&1 || true; \
	    echo ext > $(MODE); \
	fi

mode-plain: bootstrap
	@if [ "$$(cat $(MODE) 2>/dev/null)" != "plain" ]; then \
	    echo "c6: 切换到上游模式（清理旧产物）"; \
	    $(MAKE) -C $(TCC_DIR) clean >/dev/null 2>&1 || true; \
	    echo plain > $(MODE); \
	fi

build: mode-ext
	$(MAKE) -C $(TCC_DIR) -j$(JOBS) $(EXT) CFLAGS="$(CFLAGS)"

build-plain: mode-plain
	$(MAKE) -C $(TCC_DIR) -j$(JOBS) CFLAGS="$(CFLAGS)"

test: build
	$(MAKE) -C $(TCC_DIR)/tests/tests2 tests2.all

test-plain: build-plain
	$(MAKE) -C $(TCC_DIR)/tests/tests2 tests2.all

# tests/：c6 的全部测试
#   tests/*.c6         用 -test 跑里面的 test 块（同名 tests/tu/*.c6 作为第二 TU）
#   tests/c6ext/*.c6   后缀自测：不加任何开关，靠 .c6 后缀开启扩展
#   tests/errors/*.c6  编译期诊断，见 tests/run-tests.sh
#   tests/c99/*.c6     C99 输出后端：先生成 C 源码，再用 gcc 编译运行
# c6 代码只能写在 .c6 文件里：后缀是唯一的开关，命令行没有 -std=c6。
selftest: build
	./$(SELFTEST)

# 错误码号段表：errbase.def 必须与「现在 -emit-errbase 会产出的东西」一致
# （加了模块忘了钉号、或者手改表改了别人的号，都会失败），并且表的各种约束
# （C126/C127/C128）真的会报错。见 tests/run-errbase.sh。
# 注意 -emit-errbase 走的是正常编译流程，末尾会把清单里的模块连成一个可执行
# 文件：清单里不能有第二个 main，带 main 的模块文件由 run-errbase.sh 的
# MAIN_MODULES 显式排除（新增这类文件时那里会失败，提醒你去确认）。
errcheck: build
	./$(ERRBASE)

# std/：标准库。按领域分目录，一个文件一个 module，模块名就是路径
# （std/container/vec.c6 -> std.container.vec），每个文件自带 test 块，逐个跑。
#
# 模块之间可以互相 import（`std/container/vec.c6` 的 sort 就用
# `std/container/sort.c6` 的内核），而 import 只带来**声明**：被依赖模块的
# .c6 正文也要放上命令行。下面按路径顺序遍历 std/ 下所有 .c6，需要额外依赖
# 的在 case 里列出；没列出来的模块自成一体，单独跑（顺序不影响结果）。

stdtest: build
	@mkdir -p build   # std/os/io.c6 的用例要往 build/ 写临时文件，干净树上得先有目录
	@fail=0; for f in $$(find std -name '*.c6' | sort); do \
	    printf '%-26s ' "$$f"; \
	    case $$f in \
	    std/container/sort.c6) deps="std/text/str.c6" ;; \
	    std/container/vec.c6)  deps="std/container/sort.c6 std/text/str.c6" ;; \
	    std/text/strbuf.c6) deps="std/text/fmt.c6" ;; \
	    std/os/path.c6) deps="std/text/str.c6" ;; \
	    std/container/set.c6)  deps="std/container/map.c6" ;; \
	    std/container/bitset.c6) deps="std/core/bits.c6" ;; \
	    std/data/json.c6) deps="std/core/mem.c6 std/text/str.c6 std/text/num.c6 std/core/err.c6" ;; \
	    std/data/csv.c6)  deps="std/text/str.c6" ;; \
	    std/os/env.c6)  deps="std/text/str.c6 std/core/err.c6" ;; \
	    std/encoding/sha256.c6) deps="std/core/bits.c6" ;; \
	    std/net/url.c6)  deps="std/text/str.c6" ;; \
	    std/net/mime.c6) deps="std/text/str.c6 std/os/path.c6" ;; \
	    std/encoding/uuid.c6) deps="std/core/rand.c6 std/os/time.c6" ;; \
	    std/container/pqueue.c6) deps="std/container/sort.c6 std/text/str.c6" ;; \
	    std/text/width.c6) deps="std/text/utf8.c6" ;; \
	    std/encoding/punycode.c6) deps="std/text/utf8.c6" ;; \
	    std/text/regex.c6) deps="std/text/utf8.c6" ;; \
	    std/text/fuzzy.c6) deps="std/text/utf8.c6" ;; \
	    std/text/wrap.c6)  deps="std/text/width.c6 std/text/utf8.c6" ;; \
	    std/text/diff.c6)  deps="std/core/mem.c6" ;; \
	    std/core/complex.c6) deps="std/core/math.c6" ;; \
	    std/data/toml.c6)  deps="std/text/str.c6 std/text/num.c6 std/core/err.c6" ;; \
	    std/data/xml.c6)   deps="std/text/str.c6" ;; \
	    std/data/query.c6) deps="std/text/str.c6" ;; \
	    std/text/template.c6) deps="std/text/str.c6" ;; \
	    std/os/args.c6)    deps="std/text/str.c6" ;; \
	    std/text/table.c6) deps="std/text/width.c6 std/text/utf8.c6" ;; \
	    std/net/ws.c6)     deps="std/encoding/sha1.c6 std/encoding/base64.c6 std/core/err.c6" ;; \
	    std/container/chan.c6) deps="std/os/sync.c6" ;; \
	    std/net/resolver.c6) deps="std/net/dns.c6 std/net/udp.c6 std/os/io.c6 std/text/str.c6" ;; \
	    std/net/httpc.c6) deps="std/net/tcp.c6 std/net/url.c6 std/text/str.c6" ;; \
	    std/net/httpd.c6) deps="std/net/http.c6 std/net/tcp.c6 std/text/str.c6" ;; \
	    std/net/router.c6) deps="" ;; \
	    std/net/assets.c6) deps="std/net/httpd.c6 std/net/http.c6 std/net/tcp.c6 std/net/mime.c6 std/net/url.c6 std/os/fs.c6 std/os/io.c6 std/os/path.c6 std/text/str.c6 std/core/err.c6" ;; \
	    std/net/session.c6) deps="std/core/rand.c6 std/os/time.c6 std/net/cookie.c6 std/text/str.c6" ;; \
	    std/encoding/md5.c6) deps="std/core/bits.c6" ;; \
	    std/encoding/sha512.c6) deps="std/core/bits.c6" ;; \
	    std/encoding/hkdf.c6) deps="std/encoding/sha256.c6 std/core/bits.c6 std/core/err.c6" ;; \
	    std/encoding/pbkdf2.c6) deps="std/encoding/sha1.c6 std/encoding/sha256.c6 std/core/bits.c6 std/core/err.c6" ;; \
	    std/os/walk.c6) deps="std/os/fs.c6 std/os/path.c6 std/text/str.c6 std/core/err.c6" ;; \
	    std/os/temp.c6) deps="" ;; \
	    std/os/lock.c6) deps="std/os/time.c6" ;; \
	    std/os/mmap.c6) deps="" ;; \
	    std/encoding/aes.c6) deps="" ;; \
	    std/encoding/chacha20.c6) deps="std/core/bits.c6" ;; \
	    std/encoding/poly1305.c6) deps="" ;; \
	    std/encoding/random.c6) deps="" ;; \
	    std/data/bencode.c6) deps="std/text/str.c6" ;; \
	    std/data/msgpack.c6) deps="std/text/str.c6" ;; \
	    std/data/cbor.c6) deps="std/text/str.c6" ;; \
	    std/data/pem.c6) deps="std/encoding/base64.c6 std/text/str.c6" ;; \
	    std/core/geometry.c6) deps="" ;; \
	    std/core/interp.c6) deps="" ;; \
	    std/core/stats.c6) deps="" ;; \
	    std/core/matrix.c6) deps="" ;; \
	    std/encoding/deflate.c6) deps="" ;; \
	    std/encoding/zlib.c6) deps="std/encoding/deflate.c6" ;; \
	    std/encoding/gzip.c6) deps="std/encoding/deflate.c6 std/core/crc.c6" ;; \
	    std/encoding/tar.c6) deps="std/text/str.c6" ;; \
	    std/data/wav.c6) deps="" ;; \
	    std/data/png.c6) deps="std/core/crc.c6 std/text/str.c6 std/encoding/zlib.c6 std/encoding/deflate.c6" ;; \
	    std/data/elf.c6) deps="" ;; \
	    std/data/zip.c6) deps="std/encoding/deflate.c6 std/core/crc.c6" ;; \
	    std/data/bmp.c6) deps="" ;; \
	    std/data/tga.c6) deps="" ;; \
	    std/data/qoi.c6) deps="" ;; \
	    std/data/pnm.c6) deps="" ;; \
	    std/encoding/asn1.c6) deps="std/text/str.c6" ;; \
	    std/data/x509.c6) deps="std/encoding/asn1.c6 std/text/str.c6" ;; \
	    std/data/jwt.c6) deps="std/encoding/base64.c6 std/encoding/sha256.c6 std/encoding/sha512.c6 std/core/bits.c6 std/text/str.c6" ;; \
	    std/encoding/otp.c6) deps="std/encoding/sha1.c6 std/core/bits.c6" ;; \
	    std/encoding/lzw.c6) deps="" ;; \
	    std/data/gif.c6) deps="std/encoding/lzw.c6" ;; \
	    std/data/tiff.c6) deps="std/encoding/zlib.c6 std/encoding/deflate.c6 std/encoding/rle.c6" ;; \
	    std/data/ico.c6) deps="std/data/png.c6 std/core/crc.c6 std/text/str.c6 std/encoding/zlib.c6 std/encoding/deflate.c6" ;; \
	    std/data/html.c6) deps="std/text/str.c6" ;; \
	    std/data/yaml.c6) deps="std/text/str.c6" ;; \
	    std/data/vcf.c6) deps="std/text/str.c6" ;; \
	    std/data/ics.c6) deps="std/text/str.c6" ;; \
	    std/net/sse.c6) deps="std/text/str.c6" ;; \
	    std/net/socks5.c6) deps="std/net/inet.c6 std/text/str.c6 std/core/err.c6" ;; \
	    std/net/mqtt.c6) deps="std/text/str.c6" ;; \
	    std/encoding/snappy.c6) deps="" ;; \
	    std/data/cpio.c6) deps="std/text/str.c6" ;; \
	    std/data/pe.c6) deps="std/text/str.c6" ;; \
	    std/data/macho.c6) deps="std/text/str.c6" ;; \
	    std/data/wasm.c6) deps="std/text/str.c6" ;; \
	    std/data/bson.c6) deps="std/text/str.c6" ;; \
	    std/data/ubjson.c6) deps="std/text/str.c6" ;; \
	    std/data/plist.c6) deps="std/text/str.c6" ;; \
	    std/text/markdown.c6) deps="std/text/str.c6" ;; \
	    std/os/sysinfo.c6) deps="std/text/str.c6" ;; \
	    std/os/resource.c6) deps="std/text/str.c6" ;; \
	    std/os/user.c6) deps="std/text/str.c6" ;; \
	    std/os/pty.c6) deps="std/text/str.c6" ;; \
	    std/encoding/sha3.c6) deps="std/text/str.c6" ;; \
	    std/encoding/blake2s.c6) deps="std/text/str.c6" ;; \
	    std/encoding/crc32c.c6) deps="std/text/str.c6" ;; \
	    std/encoding/xxhash.c6) deps="std/text/str.c6" ;; \
	    std/data/pcx.c6) deps="std/text/str.c6" ;; \
	    std/data/hdr.c6) deps="std/text/str.c6" ;; \
	    std/data/xbm.c6) deps="std/text/str.c6" ;; \
	    std/data/xpm.c6) deps="std/text/str.c6" ;; \
	    std/data/bdf.c6) deps="std/text/str.c6" ;; \
	    std/data/woff.c6) deps="std/text/str.c6" ;; \
	    std/data/svg.c6) deps="std/text/str.c6" ;; \
	    std/data/ttf.c6) deps="std/text/str.c6" ;; \
	    std/data/mbr.c6) deps="std/text/str.c6" ;; \
	    std/data/fat.c6) deps="std/text/str.c6" ;; \
	    std/data/iso.c6) deps="std/text/str.c6" ;; \
	    std/data/ext2.c6) deps="std/text/str.c6" ;; \
	    std/encoding/base91.c6) deps="std/text/str.c6" ;; \
	    std/encoding/uu.c6) deps="std/text/str.c6" ;; \
	    std/encoding/yenc.c6) deps="std/text/str.c6" ;; \
	    std/encoding/ebcdic.c6) deps="std/text/str.c6" ;; \
	    std/data/mp3.c6) deps="std/text/str.c6" ;; \
	    std/data/ogg.c6) deps="std/text/str.c6" ;; \
	    std/data/flac.c6) deps="std/text/str.c6" ;; \
	    std/data/midi.c6) deps="std/text/str.c6" ;; \
	    std/net/smtp.c6) deps="std/text/str.c6" ;; \
	    std/net/ftp.c6) deps="std/text/str.c6" ;; \
	    std/net/redis.c6) deps="std/text/str.c6" ;; \
	    std/net/telnet.c6) deps="std/text/str.c6" ;; \
	    std/container/bloom.c6) deps="std/text/str.c6" ;; \
	    std/text/slug.c6) deps="std/text/str.c6" ;; \
	    std/text/casing.c6) deps="std/text/str.c6" ;; \
	    std/text/roman.c6) deps="std/text/str.c6" ;; \
	    std/text/phonetic.c6) deps="std/text/str.c6" ;; \
	    std/os/iso8601.c6) deps="std/text/str.c6 std/os/calendar.c6" ;; \
	    std/os/cron.c6) deps="std/text/str.c6 std/os/calendar.c6" ;; \
	    std/net/ntp.c6) deps="std/text/str.c6" ;; \
	    std/net/syslog.c6) deps="std/text/str.c6" ;; \
	    std/net/stun.c6) deps="std/text/str.c6" ;; \
	    std/net/tftp.c6) deps="std/text/str.c6" ;; \
	    std/os/log.c6) deps="std/text/str.c6" ;; \
	    std/data/sqlite.c6) deps="std/text/str.c6" ;; \
	    std/net/postgres.c6) deps="std/text/str.c6" ;; \
	    std/net/mysql.c6) deps="std/text/str.c6" ;; \
	    std/encoding/fnv.c6) deps="std/text/str.c6" ;; \
	    std/encoding/adler32.c6) deps="std/text/str.c6" ;; \
	    std/encoding/murmur3.c6) deps="std/text/str.c6" ;; \
	    std/encoding/siphash.c6) deps="std/text/str.c6" ;; \
	    std/container/counter.c6) deps="std/text/str.c6" ;; \
	    std/encoding/bwt.c6) deps="std/text/str.c6" ;; \
	    std/encoding/mtf.c6) deps="std/text/str.c6" ;; \
	    std/encoding/delta.c6) deps="std/text/str.c6" ;; \
	    std/encoding/huffman.c6) deps="std/text/str.c6" ;; \
	    std/data/srt.c6) deps="std/text/str.c6" ;; \
	    std/data/ass.c6) deps="std/text/str.c6" ;; \
	    std/data/lrc.c6) deps="std/text/str.c6" ;; \
	    std/data/m3u.c6) deps="std/text/str.c6" ;; \
	    std/data/netrc.c6) deps="std/text/str.c6" ;; \
	    std/data/sshconfig.c6) deps="std/text/str.c6" ;; \
	    std/data/keys.c6) deps="std/text/str.c6" ;; \
	    std/data/crontab.c6) deps="std/text/str.c6" ;; \
	    std/data/geohash.c6) deps="std/text/str.c6 std/core/geo.c6" ;; \
	    std/data/maidenhead.c6) deps="std/text/str.c6 std/core/geo.c6" ;; \
	    std/data/dms.c6) deps="std/text/str.c6" ;; \
	    std/data/ar.c6) deps="std/text/str.c6" ;; \
	    std/data/coff.c6) deps="std/text/str.c6" ;; \
	    std/data/class.c6) deps="std/text/str.c6" ;; \
	    std/data/rpm.c6) deps="std/text/str.c6" ;; \
	    std/encoding/luhn.c6) deps="std/text/str.c6" ;; \
	    std/encoding/crc16.c6) deps="std/text/str.c6" ;; \
	    std/encoding/hamming.c6) deps="std/text/str.c6" ;; \
	    std/encoding/classic.c6) deps="std/text/str.c6" ;; \
	    std/os/mounts.c6) deps="std/text/str.c6" ;; \
	    std/os/fstab.c6) deps="std/text/str.c6" ;; \
	    std/os/osrelease.c6) deps="std/text/str.c6" ;; \
	    std/os/systemd.c6) deps="std/text/str.c6" ;; \
	    std/core/binio.c6) deps="std/text/str.c6" ;; \
	    std/core/bitio.c6) deps="std/text/str.c6" ;; \
	    std/core/half.c6) deps="std/text/str.c6" ;; \
	    std/core/align.c6) deps="std/text/str.c6" ;; \
	    std/text/sanitize.c6) deps="std/text/str.c6" ;; \
	    std/text/tabs.c6) deps="std/text/str.c6" ;; \
	    std/text/quote.c6) deps="std/text/str.c6" ;; \
	    std/text/nato.c6) deps="std/text/str.c6" ;; \
	    std/data/palette.c6) deps="std/text/str.c6 std/core/color.c6" ;; \
	    std/text/colorname.c6) deps="std/text/str.c6 std/core/color.c6" ;; \
	    std/core/color.c6) deps="std/text/str.c6" ;; \
	    std/core/units.c6) deps="std/text/str.c6" ;; \
	    std/container/stack.c6) deps="std/text/str.c6" ;; \
	    std/container/queue.c6) deps="std/text/str.c6" ;; \
	    std/container/slots.c6) deps="std/text/str.c6" ;; \
	    std/container/bag.c6) deps="std/text/str.c6" ;; \
	    std/core/histogram.c6) deps="std/text/str.c6" ;; \
	    std/core/moving.c6) deps="std/text/str.c6" ;; \
	    std/core/series.c6) deps="std/text/str.c6" ;; \
	    std/core/quantile.c6) deps="std/text/str.c6" ;; \
	    std/text/kmp.c6) deps="std/text/str.c6" ;; \
	    std/text/bmh.c6) deps="std/text/str.c6" ;; \
	    std/text/suffix.c6) deps="std/text/str.c6" ;; \
	    std/text/wordplay.c6) deps="std/text/str.c6" ;; \
	    std/data/aiff.c6) deps="std/text/str.c6" ;; \
	    std/data/au.c6) deps="std/text/str.c6" ;; \
	    std/data/voc.c6) deps="std/text/str.c6" ;; \
	    std/data/ircam.c6) deps="std/text/str.c6" ;; \
	    std/net/irc.c6) deps="std/text/str.c6" ;; \
	    std/net/gopher.c6) deps="std/text/str.c6" ;; \
	    std/net/whois.c6) deps="std/text/str.c6" ;; \
	    std/net/ident.c6) deps="std/text/str.c6" ;; \
	    std/text/rst.c6) deps="std/text/str.c6" ;; \
	    std/text/org.c6) deps="std/text/str.c6" ;; \
	    std/text/tex.c6) deps="std/text/str.c6" ;; \
	    std/text/pod.c6) deps="std/text/str.c6" ;; \
	    std/text/words.c6) deps="std/text/str.c6" ;; \
	    std/text/sentences.c6) deps="std/text/str.c6" ;; \
	    std/text/ngram.c6) deps="std/text/str.c6" ;; \
	    std/text/readability.c6) deps="std/text/str.c6 std/text/words.c6 std/text/sentences.c6" ;; \
	    std/data/dotenv.c6) deps="std/text/str.c6" ;; \
	    std/data/hosts.c6) deps="std/text/str.c6" ;; \
	    std/data/resolvconf.c6) deps="std/text/str.c6" ;; \
	    std/data/knownhosts.c6) deps="std/text/str.c6" ;; \
	    std/text/aho.c6) deps="std/text/str.c6" ;; \
	    std/text/rabin.c6) deps="std/text/str.c6" ;; \
	    std/container/interval.c6) deps="std/text/str.c6" ;; \
	    std/text/align.c6) deps="std/text/str.c6" ;; \
	    std/core/future.c6) deps="std/text/str.c6" ;; \
	    std/core/timer.c6) deps="std/text/str.c6" ;; \
	    std/core/task.c6) deps="std/text/str.c6" ;; \
	    std/os/reactor.c6) deps="std/text/str.c6 std/os/poll.c6 std/core/timer.c6" ;; \
	    std/core/executor.c6) deps="std/text/str.c6 std/os/poll.c6 std/core/timer.c6 std/core/task.c6 std/os/reactor.c6" ;; \
	    std/os/asio.c6) deps="std/text/str.c6 std/os/poll.c6 std/core/timer.c6 std/os/reactor.c6" ;; \
	    std/core/retry.c6) deps="std/text/str.c6" ;; \
	    std/core/awaitgroup.c6) deps="std/text/str.c6 std/core/future.c6" ;; \
	    std/core/solve.c6) deps="std/text/str.c6 -lm" ;; \
	    std/core/ode.c6) deps="std/text/str.c6 -lm" ;; \
	    std/core/optimize.c6) deps="std/text/str.c6 -lm" ;; \
	    std/core/least.c6) deps="std/text/str.c6 -lm" ;; \
	    std/core/lu.c6) deps="std/text/str.c6 -lm" ;; \
	    std/core/eigen.c6) deps="std/text/str.c6 -lm" ;; \
	    std/core/tridiag.c6) deps="std/text/str.c6 -lm" ;; \
	    std/core/sparse.c6) deps="std/text/str.c6 -lm" ;; \
	    std/text/inverted.c6) deps="std/text/str.c6" ;; \
	    std/text/stem.c6) deps="std/text/str.c6" ;; \
	    std/text/keyword.c6) deps="std/text/str.c6" ;; \
	    std/text/bm25.c6) deps="std/text/str.c6 -lm" ;; \
	    std/container/mst.c6) deps="std/text/str.c6" ;; \
	    std/container/flow.c6) deps="std/text/str.c6" ;; \
	    std/container/scc.c6) deps="std/text/str.c6" ;; \
	    std/container/match.c6) deps="std/text/str.c6" ;; \
	    *)           deps="" ;; \
	    esac; \
	    out=$$(./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -test $$f $$deps 2>&1) || fail=1; \
	    echo "$$out" | tail -1; \
	done; \
	test $$fail -eq 0

# std-demo 的源文件清单：示例两个文件 + 用到的模块正文。清单必须覆盖被
# import 模块的正文（import 只带来声明）——`std/container/vec.c6` 的排序内核在
# `std/container/sort.c6` 里，sort 自己又依赖 std/text/str.c6、std/core/err.c6、std/core/types.c6。
# 改动 std/ 里的 import 时，这里和上面 stdtest 的 case 要一起更新。
STDDEMO_SRCS := examples/wordcount.c6 examples/wordcount/text.c6 \
                std/core/types.c6 std/core/err.c6 std/text/str.c6 std/container/sort.c6 \
                std/container/vec.c6 std/container/span.c6 std/text/fmt.c6 std/container/range.c6

# 标准库的拼装示例：模块正文要单独编进程序（import 只带来声明），
# 模块搜索路径靠 -I 给出（import wordcount.text 找 examples/wordcount/text.c6）。
std-demo: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I examples -I . -o build/wordcount $(STDDEMO_SRCS)
	./build/wordcount

# 标准库一日游：把新加的模块（strbuf/utf8/bits/hash/path/deque）串成一个
# 程序，顺带当它们的集成用例。模块正文照样要一个个列出来。
STDTOUR_SRCS := examples/stdtour.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 \
                std/container/span.c6 std/text/fmt.c6 std/text/strbuf.c6 std/text/utf8.c6 \
                std/core/bits.c6 std/core/hash.c6 std/os/path.c6 std/container/deque.c6

std-tour: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdtour $(STDTOUR_SRCS)
	./build/stdtour

# 标准库第三批的一日游：ascii / num / codec / rand / fs / set。
# set 是 map 的薄包装，所以 map 的正文也要在命令行上。
STDKIT_SRCS := examples/stdkit.c6 std/core/types.c6 std/core/err.c6 std/container/map.c6 \
               std/container/set.c6 std/text/ascii.c6 std/text/num.c6 std/encoding/codec.c6 \
               std/core/rand.c6 std/os/fs.c6

std-kit: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdkit $(STDKIT_SRCS)
	./build/stdkit

# 标准库第四批：json / sha256 / csv / env。
# json 的 DOM 住在 std.core.mem 的 Arena 上，字符串视图来自 std.text.str，数字格式化
# 用 std.text.num；csv 也借 std.text.str 的 Str；env 依赖 std.text.str 与 std.core.err。
STDBOX_SRCS := examples/stdbox.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 \
               std/core/mem.c6 std/text/num.c6 std/data/json.c6 std/core/bits.c6 \
               std/encoding/sha256.c6 std/data/csv.c6 std/os/env.c6

std-box: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdbox $(STDBOX_SRCS)
	./build/stdbox

# 标准库第五批：base64 / url / uuid / mime。
# url 与 mime 只借 std.text.str 的 Str 与 std.os.path 的取扩展名；uuid 的 v4/v7 要
# std.core.rand 的随机源、v7 还要 std.os.time 的墙钟；base64 自成一体。
STD_WEB_SRCS := examples/stdweb.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 \
                std/os/path.c6 std/encoding/base64.c6 std/net/url.c6 std/core/rand.c6 \
                std/os/time.c6 std/encoding/uuid.c6 std/net/mime.c6

std-web: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdweb $(STD_WEB_SRCS)
	./build/stdweb

# 标准库第六批：inet / bitbuf / pqueue / ini。
# inet 与 bitbuf 自成一体（只用 std.core.types 的定宽整数与 err() 内建）；
# pqueue 要 std.container.sort 的比较器（cmp_str 又拉上 std.text.str）；ini 借 std.text.str 的 Str。
STD_NET_SRCS := examples/stdnet.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 \
                std/container/sort.c6 std/net/inet.c6 std/encoding/bitbuf.c6 std/container/pqueue.c6 \
                std/data/ini.c6

std-net: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdnet $(STD_NET_SRCS)
	./build/stdnet

# 标准库第七批：bitset / glob / base32 / http。
# bitset 要 std.core.bits 的 popcount/ctz；glob 与 http 只借 std.text.str 的 Str 类型。
STDMORE_SRCS := examples/stdmore.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 \
                std/core/bits.c6 std/container/bitset.c6 std/text/glob.c6 \
                std/encoding/base32.c6 std/net/http.c6

std-more: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdmore $(STDMORE_SRCS)
	./build/stdmore

# 标准库第八批：base58 / varint / semver / cookie。
STDNEXT_SRCS := examples/stdnext.c6 std/core/types.c6 std/core/err.c6 \
                std/text/str.c6 std/encoding/base58.c6 std/encoding/varint.c6 \
                std/text/semver.c6 std/net/cookie.c6

std-next: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdnext $(STDNEXT_SRCS)
	./build/stdnext

# 标准库第九批：list / ring / utf16 / ansi。
STDNODE_SRCS := examples/stdnode.c6 std/core/types.c6 std/core/err.c6 \
                std/container/list.c6 std/container/ring.c6 \
                std/text/utf16.c6 std/text/ansi.c6

std-node: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdnode $(STDNODE_SRCS)
	./build/stdnode

# 标准库第十批：props / multipart / punycode / width。
STDFORM_SRCS := examples/stdform.c6 std/core/types.c6 std/core/err.c6 \
                std/text/utf8.c6 std/data/props.c6 \
                std/net/multipart.c6 std/encoding/punycode.c6 std/text/width.c6

std-form: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdform $(STDFORM_SRCS)
	./build/stdform

# 标准库第十一批（嵌入式基础）：crc / fixed / pool / frame。
# 四个模块都只依赖 std.core.types，frame 的 COBS/SLIP 与 crc 的校验和都
# 是零分配原语；示例把它们拼成一条「采样 -> 定点 -> 分帧 -> 校验 -> 池」链路。
STDEMBED_SRCS := examples/stdembed.c6 std/core/types.c6 std/core/err.c6 \
                 std/core/crc.c6 std/core/fixed.c6 std/core/pool.c6 \
                 std/encoding/frame.c6

std-embed: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdembed $(STDEMBED_SRCS)
	./build/stdembed

# 标准库第十二批（文本算法）：regex / diff / fuzzy / wrap。
# regex 与 fuzzy 依赖 std.text.utf8，wrap 再依赖 std.text.width（显示列宽），
# diff 借 std.core.mem 的 Arena 存行表与 LCS 工作区。示例把四块串成
# 「日志筛选 -> 命令补全 -> 帮助折行 -> 配置对比」一条链路。
STDTEXT_SRCS := examples/stdtext.c6 std/core/types.c6 std/core/mem.c6 \
                std/text/regex.c6 std/text/utf8.c6 std/text/diff.c6 \
                std/text/fuzzy.c6 std/text/wrap.c6 std/text/width.c6

std-text: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdtext $(STDTEXT_SRCS)
	./build/stdtext

# 标准库第十三批（数值类型）：bignum / decimal / rational / complex。
# bignum 与 decimal/rational 只依赖 std.core.types（128 位小工具各自内联），
# complex 用 std.core.math 的 sqrt/nearly_eq。示例把四块串成
# 「大整数阶乘 -> 金额税额 -> 调和级数 -> 复数」一条链路。
STDNUM_SRCS := examples/stdnum.c6 std/core/types.c6 std/core/math.c6 \
               std/core/bignum.c6 std/core/decimal.c6 std/core/rational.c6 \
               std/core/complex.c6

std-num: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdnum $(STDNUM_SRCS)
	./build/stdnum

# 标准库第十四批（压缩与文本编码）：lz4 / rle / base85 / hexdump。
# 四个模块都只依赖 std.core.types（lz4 的哈希表与所有输出缓冲都由调用方给）。
# 示例把四块串成「遥测帧 LZ4 -> 采样差分 + RLE -> Ascii85/Z85 -> hexdump」。
STDPACK_SRCS := examples/stdpack.c6 std/core/types.c6 \
                std/encoding/lz4.c6 std/encoding/rle.c6 \
                std/encoding/base85.c6 std/encoding/hexdump.c6

std-pack: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdpack $(STDPACK_SRCS)
	./build/stdpack

# 标准库第十五批（配置与文档格式）：toml / xml / query / template。
# toml 依赖 std.text.str 与 std.text.num（浮点解析），xml/query/template 只依赖
# std.text.str。示例把四块串成「读配置 -> 扫文档 -> 解析查询串 -> 填模板」。
STDCONF_SRCS := examples/stdconf.c6 std/core/types.c6 std/core/err.c6 \
                std/text/str.c6 std/text/num.c6 std/data/toml.c6 \
                std/data/xml.c6 std/data/query.c6 std/text/template.c6

std-conf: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdconf $(STDCONF_SRCS)
	./build/stdconf

# 标准库第十六批（命令行工具基础）：args / table / natural / proc。
# args 依赖 std.text.str，table 依赖 std.text.width（显示列宽），natural 与
# proc 只依赖 types。示例把四块串成「解析参数 -> 自然排序/人类可读 -> 表格 ->
# 跑子进程」。
STDCLI_SRCS := examples/stdcli.c6 std/core/types.c6 std/text/str.c6 \
               std/text/utf8.c6 std/text/width.c6 std/os/args.c6 \
               std/text/table.c6 std/text/natural.c6 std/os/proc.c6

std-cli: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcli $(STDCLI_SRCS)
	./build/stdcli

# 标准库第十七批（网络协议基础）：sha1 / ws / dns / tcp。
# ws 依赖 sha1 与 base64（握手 accept-key），dns/tcp 只依赖 types（tcp 是
# POSIX socket 薄封装）。示例把四块串成「摘要 -> 握手/分帧 -> DNS 报文 ->
# loopback TCP 往返」。
STDWIRE_SRCS := examples/stdwire.c6 std/core/types.c6 std/core/err.c6 \
                std/encoding/base64.c6 std/encoding/sha1.c6 std/net/ws.c6 \
                std/net/dns.c6 std/net/tcp.c6

std-wire: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdwire $(STDWIRE_SRCS)
	./build/stdwire

# 标准库第十八批（算法与容器）：graph / lru / trie / region。
# 四个模块都只依赖 std.core.types（所有数组/节点池都由调用方给）。示例把四块
# 串成「依赖拓扑排序 + 最短路 -> LRU 缓存 -> 前缀树 -> 地址区间」。
STDALGO_SRCS := examples/stdalgo.c6 std/core/types.c6 \
                std/container/graph.c6 std/container/lru.c6 \
                std/container/trie.c6 std/container/region.c6

std-algo: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdalgo $(STDALGO_SRCS)
	./build/stdalgo

# 标准库第十九批（并发与同步）：atomic / sync / thread / chan。
# atomic 只依赖 types；sync 是 pthread 薄封装；thread 的线程池用 pthread_create
# 起裸线程；chan 建在 sync 的 Mutex/Cond 上。示例把四块串成「原子计数/自旋锁
# -> 互斥锁+条件变量握手 -> 线程池跑任务 -> 通道收发」。
STDTHREAD_SRCS := examples/stdthread.c6 std/core/types.c6 std/core/atomic.c6 \
                  std/os/sync.c6 std/os/thread.c6 std/container/chan.c6

std-thread: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdthread $(STDTHREAD_SRCS)
	./build/stdthread

# 标准库第二十批（终端与进程控制）：tty / signal / pipe / poll。
# tty 是 termios 的薄壳（isatty、窗口大小、原始模式）；signal 包 sigaction/
# sigprocmask；pipe 管一对 fd 的读写与 EOF；poll 把 poll(2) 包成 PollSet，
# 示例走一遍「伪终端 -> 信号 -> 管道 -> 多路等待」。
STDTERM_SRCS := examples/stdterm.c6 std/core/types.c6 std/os/tty.c6 \
                std/os/signal.c6 std/os/pipe.c6 std/os/poll.c6

std-term: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdterm $(STDTERM_SRCS)
	./build/stdterm

# 标准库第二十一批（网络客户端）：udp / uds / resolver / httpc。
# udp 是数据报薄封装；uds 是 AF_UNIX 本地套接字；resolver 读 resolv.conf 并用
# UDP 发 DNS 查询；httpc 在 tcp+url 之上拼请求、解 chunked。示例里的两个
# 「服务器」都用 fork 起在 127.0.0.1 上，所以不联网也能跑。
STDCLIENT_SRCS := examples/stdclient.c6 std/core/types.c6 std/core/err.c6 \
                  std/net/udp.c6 std/net/uds.c6 std/net/dns.c6 \
                  std/net/resolver.c6 std/os/io.c6 std/text/str.c6 \
                  std/net/tcp.c6 std/net/url.c6 std/net/httpc.c6

std-client: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdclient $(STDCLIENT_SRCS)
	./build/stdclient

# 标准库第二十二批（Web 服务端）：httpd / router / assets / session。
# httpd 在 http+tcp 上收请求写响应；router 做方法与路径模式匹配；assets 把
# root 下的文件当静态资源发（带路径越界防护）；session 管内存会话与 Cookie。
# 示例 fork 一个本地服务端、再用第二十一批的 httpc 打回去，离线可跑。
STDSERVE_SRCS := examples/stdserve.c6 std/core/types.c6 std/core/err.c6 \
                 std/net/httpd.c6 std/net/http.c6 std/net/tcp.c6 \
                 std/net/router.c6 std/net/assets.c6 std/net/mime.c6 \
                 std/net/url.c6 std/os/fs.c6 std/os/io.c6 std/os/path.c6 \
                 std/text/str.c6 std/net/session.c6 std/core/rand.c6 \
                 std/os/time.c6 std/net/cookie.c6 std/net/httpc.c6

std-serve: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdserve $(STDSERVE_SRCS)
	./build/stdserve

# 标准库第二十三批（消息摘要与密钥派生）：md5 / sha512 / hkdf / pbkdf2。
# md5 是兼容用的校验和；sha512 补 SHA-512/384 与 HMAC-SHA-512；hkdf 在
# HMAC-SHA-256 上做提取/扩展；pbkdf2 是带迭代的口令 KDF（SHA-1/SHA-256）。
# 全部是纯计算，期望值来自 RFC 向量，所以输出与平台无关。
STDCRYPTO_SRCS := examples/stdcrypto.c6 std/core/types.c6 std/core/err.c6 \
                  std/core/bits.c6 std/encoding/md5.c6 std/encoding/sha1.c6 \
                  std/encoding/sha256.c6 std/encoding/sha512.c6 \
                  std/encoding/hkdf.c6 std/encoding/pbkdf2.c6 std/os/time.c6

std-crypto: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcrypto $(STDCRYPTO_SRCS)
	./build/stdcrypto

# 标准库第二十四批（文件系统进阶）：walk / temp / lock / mmap。
# walk 把 fs 的单层 Dir 包成整棵树的遍历与删除；temp 用 mkstemp/mkdtemp 安全地
# 拿临时名字；lock 是 fcntl 建议锁（含超时等待）；mmap 做文件/匿名映射。
# 示例走一遍「临时文件 -> 就地改字节 -> 扫目录 -> 跨进程抢锁」。
STDFSX_SRCS := examples/stdfsx.c6 std/core/types.c6 std/core/err.c6 \
               std/os/walk.c6 std/os/temp.c6 std/os/lock.c6 std/os/mmap.c6 \
               std/os/fs.c6 std/os/path.c6 std/os/time.c6 std/text/str.c6

std-fsx: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdfsx $(STDFSX_SRCS)
	./build/stdfsx

# 标准库第二十五批（对称加密基础）：aes / chacha20 / poly1305 / random。
# aes 是 FIPS 197 的分组密码与 CTR；chacha20 是 RFC 8439 的流密码；
# poly1305 是一次性 MAC；random 从 /dev/urandom 取安全随机。示例把四节串起来，
# 最后做一次 ChaCha20 + Poly1305 的 encrypt-then-MAC（含篡改检测）。
STDCIPHER_SRCS := examples/stdcipher.c6 std/core/types.c6 std/core/bits.c6 \
                  std/encoding/aes.c6 std/encoding/chacha20.c6 \
                  std/encoding/poly1305.c6 std/encoding/random.c6

std-cipher: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcipher $(STDCIPHER_SRCS)
	./build/stdcipher

# 标准库第二十六批（数据格式）：bencode / msgpack / cbor / pem。
# bencode 是 BitTorrent 的元信息格式；msgpack 与 cbor 是两种紧凑二进制；
# pem 是 BEGIN/END + Base64 的封装（接 std.encoding.base64）。都是纯函数、
# 零分配，示例用同一份小数据走一遍四种格式。
STDDATA_SRCS := examples/stddata.c6 std/core/types.c6 std/text/str.c6 \
                std/data/bencode.c6 std/data/msgpack.c6 std/data/cbor.c6 \
                std/data/pem.c6 std/encoding/base64.c6

std-data: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stddata $(STDDATA_SRCS)
	./build/stddata

# 标准库第二十七批（数值工具）：geometry / interp / stats / matrix。
# geometry 是 Vec2/Vec3/Rect；interp 是插值/重映射/缓动/Catmull-Rom；
# stats 是 Welford 流式统计与分位数/直方图/相关；matrix 是行主序矩阵与
# 行列式/线性方程组。四个模块都是纯函数、零分配。
STDCALC_SRCS := examples/stdcalc.c6 std/core/types.c6 std/core/geometry.c6 \
                std/core/interp.c6 std/core/stats.c6 std/core/matrix.c6

std-calc: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcalc $(STDCALC_SRCS)
	./build/stdcalc

# 标准库第二十八批（压缩与归档）：deflate / zlib / gzip / tar。
# deflate 是完整 inflate + fixed-Huffman deflate；zlib/gzip 是两种容器（含
# Adler-32 / CRC-32 校验）；tar 是 ustar 归档的读写。示例按依赖顺序串起来，
# 最后做一次 .tar.gz 的组合。
STDARCH_SRCS := examples/stdarch.c6 std/core/types.c6 std/core/crc.c6 \
                std/encoding/deflate.c6 std/encoding/zlib.c6 \
                std/encoding/gzip.c6 std/encoding/tar.c6 std/text/str.c6

std-arch: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdarch $(STDARCH_SRCS)
	./build/stdarch

# 标准库第二十九批（二进制文件格式）：wav / png / elf / zip。
# 四个模块都只读不拷贝：wav/png 解析容器块，elf 读头与节表，zip 走中央目录 +
# 本地头 + stored/deflate。示例嵌了四段真实字节（Python 生成），逐段自检。
STD_BIN_SRCS := examples/stdbin.c6 std/core/types.c6 std/core/crc.c6 \
                std/encoding/deflate.c6 std/encoding/zlib.c6 \
                std/data/wav.c6 std/data/png.c6 std/data/elf.c6 \
                std/data/zip.c6 std/text/str.c6

std-bin: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdbin $(STD_BIN_SRCS)
	./build/stdbin

# 标准库第三十批（图像解码）：bmp / tga / qoi / pnm。
# 四个模块都解成 RGBA8888，示例读同一张 2x2 图（红/绿、蓝/白）逐像素比对，
# 再用 QOI 的编码器写回去解一遍。
STD_IMG_SRCS := examples/stdimg.c6 std/core/types.c6 std/data/bmp.c6 \
                std/data/tga.c6 std/data/qoi.c6 std/data/pnm.c6

std-img: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdimg $(STD_IMG_SRCS)
	./build/stdimg

# 标准库第三十一批（编码与凭据）：asn1 / x509 / jwt / otp。
# 示例先走一遍手写 DER，再解一张自签证书、验一个 HS256 令牌、打几个 HOTP/TOTP 码。
STD_ENC_SRCS := examples/stdenc.c6 std/core/types.c6 std/core/bits.c6 \
                std/text/str.c6 std/encoding/asn1.c6 std/encoding/base64.c6 \
                std/encoding/sha1.c6 std/encoding/sha256.c6 \
                std/encoding/sha512.c6 std/encoding/otp.c6 \
                std/data/x509.c6 std/data/jwt.c6

std-enc: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdenc $(STD_ENC_SRCS)
	./build/stdenc

# 标准库第三十二批（图像与 LZW）：lzw / gif / tiff / ico（并给 png 补 png_decode）。
# 示例做 LZW 往返、解四种格式，最后把 ICO 里嵌的 PNG 单独解一遍。
STD_GFX_SRCS := examples/stdgfx.c6 std/core/types.c6 std/core/crc.c6 \
                std/text/str.c6 std/encoding/lzw.c6 std/encoding/zlib.c6 \
                std/encoding/deflate.c6 std/encoding/rle.c6 std/data/png.c6 \
                std/data/gif.c6 std/data/tiff.c6 std/data/ico.c6

std-gfx: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdgfx $(STD_GFX_SRCS)
	./build/stdgfx

# 标准库第三十三批（结构化文本格式）：html / yaml / vcf / ics。
# 示例把四个读取器串起来：HTML 分词、YAML 遍历、vCard 与 iCalendar 折行摊平。
STD_DOC_SRCS := examples/stddoc.c6 std/core/types.c6 std/text/str.c6 \
                std/data/html.c6 std/data/yaml.c6 std/data/vcf.c6 \
                std/data/ics.c6

std-doc: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stddoc $(STD_DOC_SRCS)
	./build/stddoc

# 标准库第三十四批（网络协议编解码）：sse / socks5 / mqtt / snappy。
# 示例把四个字节层编解码器串起来：事件流、握手报文、报文头与主题通配、压缩往返。
STD_PROTO_SRCS := examples/stdproto.c6 std/core/types.c6 std/core/err.c6 \
                  std/text/str.c6 std/net/inet.c6 std/net/sse.c6 \
                  std/net/socks5.c6 std/net/mqtt.c6 std/encoding/snappy.c6

std-proto: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdproto $(STD_PROTO_SRCS)
	./build/stdproto

# 标准库第三十五批（二进制容器格式）：cpio / pe / macho / wasm。
# 示例在内存里现场拼出四种容器再解析：归档、PE32+ 节表与 RVA、Mach-O 段/节、
# wasm 导出表。
STD_EXEC_SRCS := examples/stdexec.c6 std/core/types.c6 std/text/str.c6 \
                 std/data/cpio.c6 std/data/pe.c6 std/data/macho.c6 \
                 std/data/wasm.c6

std-exec: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdexec $(STD_EXEC_SRCS)
	./build/stdexec

# 标准库第三十六批（序列化与标记）：bson / ubjson / plist / markdown。
# 示例现场拼 BSON 与 UBJSON 文档、解一段 plistlib 生成的二进制 plist，
# 再把一段 Markdown 渲染成 HTML。
STD_SER_SRCS := examples/stdser.c6 std/core/types.c6 std/text/str.c6 \
                std/data/bson.c6 std/data/ubjson.c6 std/data/plist.c6 \
                std/text/markdown.c6

std-ser: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdser $(STD_SER_SRCS)
	./build/stdser

# 标准库第三十七批（系统视图）：sysinfo / resource / user / pty。
# 示例打印 uname/负载/运行时长/rlimit/rusage/当前用户，再用伪终端跑一个子进程。
STD_SYS_SRCS := examples/stdsys.c6 std/core/types.c6 std/text/str.c6 \
                std/os/sysinfo.c6 std/os/resource.c6 std/os/user.c6 \
                std/os/pty.c6

std-sys: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdsys $(STD_SYS_SRCS)
	./build/stdsys

# 标准库第三十八批（哈希与校验）：sha3 / blake2s / crc32c / xxhash。
# 示例拿已知向量对四种算法各算一遍，再验证流式与一次性结果一致。
STD_HASH_SRCS := examples/stdhash.c6 std/core/types.c6 std/text/str.c6 \
                 std/encoding/sha3.c6 std/encoding/blake2s.c6 \
                 std/encoding/crc32c.c6 std/encoding/xxhash.c6

std-hash: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdhash $(STD_HASH_SRCS)
	./build/stdhash

# 标准库第三十九批（老图形格式）：pcx / hdr / xbm / xpm。
# 示例现场拼四种老格式再解析：PCX 调色板位图、Radiance RGBE、XBM 位图、XPM 彩图。
STD_LEGACY_SRCS := examples/stdlegacy.c6 std/core/types.c6 std/text/str.c6 \
                   std/data/pcx.c6 std/data/hdr.c6 std/data/xbm.c6 \
                   std/data/xpm.c6

std-legacy: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdlegacy $(STD_LEGACY_SRCS)
	./build/stdlegacy

# 标准库第四十批（字体与矢量）：bdf / woff / svg / ttf。
# 示例现场拼 BDF 字体、WOFF 容器、SVG 路径与最小 TrueType 再解析。
STD_FONT_SRCS := examples/stdfont.c6 std/core/types.c6 std/text/str.c6 \
                 std/data/bdf.c6 std/data/woff.c6 std/data/svg.c6 \
                 std/data/ttf.c6

std-font: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdfont $(STD_FONT_SRCS) -lm
	./build/stdfont

# 标准库第四十一批（分区与文件系统）：mbr / fat / iso / ext2。
# 示例现场拼 MBR+GPT、FAT16 BPB、ISO 9660 卷与 ext2 超级块再解析。
STD_DISK_SRCS := examples/stddisk.c6 std/core/types.c6 std/text/str.c6 \
                 std/data/mbr.c6 std/data/fat.c6 std/data/iso.c6 \
                 std/data/ext2.c6

std-disk: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stddisk $(STD_DISK_SRCS)
	./build/stddisk

# 标准库第四十二批（老式文本编码）：base91 / uu / yenc / ebcdic。
# 示例把四种编码各走一遍：编码 → 解回原文。
STD_TRANSCODE_SRCS := examples/stdtranscode.c6 std/core/types.c6 \
                      std/text/str.c6 std/encoding/base91.c6 \
                      std/encoding/uu.c6 std/encoding/yenc.c6 \
                      std/encoding/ebcdic.c6

std-transcode: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdtranscode $(STD_TRANSCODE_SRCS)
	./build/stdtranscode

# 标准库第四十三批（音频容器）：mp3 / ogg / flac / midi。
# 示例现场拼 MP3 标签与帧头、Ogg 页与 Opus 头、FLAC 元数据块、MIDI 轨事件。
STD_AUDIO_SRCS := examples/stdaudio.c6 std/core/types.c6 std/text/str.c6 \
                  std/data/mp3.c6 std/data/ogg.c6 std/data/flac.c6 \
                  std/data/midi.c6

std-audio: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdaudio $(STD_AUDIO_SRCS)
	./build/stdaudio

# 标准库第四十四批（文本行协议）：smtp / ftp / redis / telnet。
# 示例把四种协议的消息形态各走一遍（应答/命令/列表行/RESP/IAC）。
STD_PROTO2_SRCS := examples/stdproto2.c6 std/core/types.c6 std/text/str.c6 \
                   std/net/smtp.c6 std/net/ftp.c6 std/net/redis.c6 \
                   std/net/telnet.c6

std-proto2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdproto2 $(STD_PROTO2_SRCS)
	./build/stdproto2

# 标准库第四十五批（基础数据结构）：fenwick / segtree / dsu / bloom。
# 示例把四种结构各跑一遍（前缀和、区间最值、合并集合、布隆查询）。
STD_DS_SRCS := examples/stdstruct.c6 std/core/types.c6 std/text/str.c6 \
               std/container/fenwick.c6 std/container/segtree.c6 \
               std/container/dsu.c6 std/container/bloom.c6

std-ds: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdstruct $(STD_DS_SRCS)
	./build/stdstruct

# 标准库第四十六批（文本变换）：slug / casing / roman / phonetic。
# 示例把四种变换各走一遍（slug 化、命名风格互转、罗马数字往返、发音编码）。
STD_WORDS_SRCS := examples/stdwords.c6 std/core/types.c6 std/text/str.c6 \
                  std/text/slug.c6 std/text/casing.c6 std/text/roman.c6 \
                  std/text/phonetic.c6

std-words: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdwords $(STD_WORDS_SRCS)
	./build/stdwords

# 标准库第四十七批（数值工具）：vector / polynomial / fft / spline。
# 示例把四种数值工具各跑一遍（矢量/多项式/FFT 往返/曲线采样），链接带 -lm。
STD_MATH2_SRCS := examples/stdnumeric.c6 std/core/types.c6 \
                  std/core/vector.c6 std/core/polynomial.c6 std/core/fft.c6 \
                  std/core/spline.c6

std-math2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdnumeric $(STD_MATH2_SRCS) -lm
	./build/stdnumeric

# 标准库第四十八批（时间工具）：calendar / iso8601 / duration / cron。
# 示例把四种时间工具各走一遍（日历换算、ISO 解析/格式化、时长、cron 下次运行）。
STD_TIME_SRCS := examples/stdtime.c6 std/core/types.c6 std/text/str.c6 \
                 std/os/calendar.c6 std/os/iso8601.c6 std/os/duration.c6 \
                 std/os/cron.c6

std-time: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdtime $(STD_TIME_SRCS)
	./build/stdtime

# 标准库第四十九批（网络报文）：ntp / syslog / stun / tftp。
# 示例把四种报文格式各走一遍（时间戳换算、syslog 两种 RFC、STUN 属性、TFTP 往返）。
STD_PACKET_SRCS := examples/stdpacket.c6 std/core/types.c6 std/text/str.c6 \
                   std/net/ntp.c6 std/net/syslog.c6 std/net/stun.c6 \
                   std/net/tftp.c6

std-packet: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdpacket $(STD_PACKET_SRCS)
	./build/stdpacket

# 标准库第五十批（日志与数据库）：log / sqlite / postgres / mysql。
# 示例把环形日志、SQLite 页、PostgreSQL 报文与 MySQL 握手各走一遍。
STD_DB_SRCS := examples/stddb.c6 std/core/types.c6 std/text/str.c6 \
               std/os/log.c6 std/data/sqlite.c6 std/net/postgres.c6 \
               std/net/mysql.c6

std-db: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stddb $(STD_DB_SRCS)
	./build/stddb

# 标准库第五十一批（非加密散列补齐）：fnv / adler32 / murmur3 / siphash。
# 示例把四种散列各跑一遍（含官方向量与流式接口）。
STD_HASH2_SRCS := examples/stdhash2.c6 std/core/types.c6 std/text/str.c6 \
                  std/encoding/fnv.c6 std/encoding/adler32.c6 \
                  std/encoding/murmur3.c6 std/encoding/siphash.c6

std-hash2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdhash2 $(STD_HASH2_SRCS)
	./build/stdhash2

# 标准库第五十二批（有序容器）：treap / heap / skiplist / counter。
# 示例把四种有序结构各跑一遍（排名/第 k 小、top-k、有序遍历、频次统计）。
STD_ORDERED_SRCS := examples/stdordered.c6 std/core/types.c6 std/text/str.c6 \
                    std/container/treap.c6 std/container/heap.c6 \
                    std/container/skiplist.c6 std/container/counter.c6

std-ordered: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdordered $(STD_ORDERED_SRCS)
	./build/stdordered

# 标准库第五十三批（变换式压缩积木）：bwt / mtf / delta / huffman。
# 示例演示 bzip2 前半段（BWT → MTF）+ 差分与规范 Huffman 的往返。
STD_BWT_SRCS := examples/stdcmp.c6 std/core/types.c6 std/text/str.c6 \
                std/encoding/bwt.c6 std/encoding/mtf.c6 \
                std/encoding/delta.c6 std/encoding/huffman.c6

std-bwt: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcmp $(STD_BWT_SRCS)
	./build/stdcmp

# 标准库第五十四批（字幕与播放列表）：srt / ass / lrc / m3u。
# 示例用游标方法（doc.open / doc.next / doc.count）把四种文本格式各走一遍。
STD_SUBS_SRCS := examples/stdsubs.c6 std/core/types.c6 std/text/str.c6 \
                 std/data/srt.c6 std/data/ass.c6 std/data/lrc.c6 \
                 std/data/m3u.c6

std-subs: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdsubs $(STD_SUBS_SRCS)
	./build/stdsubs

# 标准库第五十五批（凭据与配置）：netrc / sshconfig / keys / crontab。
# 示例用游标方法把四种配置格式各走一遍（含 ssh 主机匹配与 crontab 变量）。
STD_CREDS_SRCS := examples/stdcreds.c6 std/core/types.c6 std/text/str.c6 \
                  std/data/netrc.c6 std/data/sshconfig.c6 \
                  std/data/keys.c6 std/data/crontab.c6

std-creds: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcreds $(STD_CREDS_SRCS)
	./build/stdcreds

# 标准库第五十六批（地理编码）：geo / geohash / maidenhead / dms。
# 示例把球面距离、Geohash、Maidenhead 网格与度分秒各走一遍，链接带 -lm。
STD_GEO_SRCS := examples/stdgeo.c6 std/core/types.c6 std/text/str.c6 \
                std/core/geo.c6 std/data/geohash.c6 \
                std/data/maidenhead.c6 std/data/dms.c6

std-geo: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdgeo $(STD_GEO_SRCS) -lm
	./build/stdgeo

# 标准库第五十七批（目标文件与包）：ar / coff / class / rpm。
# 示例用游标方法把归档、COFF、Java class 与 RPM 头各走一遍。
STD_PKG_SRCS := examples/stdpkg.c6 std/core/types.c6 std/text/str.c6 \
                std/data/ar.c6 std/data/coff.c6 std/data/class.c6 \
                std/data/rpm.c6

std-pkg: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdpkg $(STD_PKG_SRCS)
	./build/stdpkg

# 标准库第五十八批（校验与纠错）：luhn / crc16 / hamming / classic。
# 示例把校验位、CRC 流式、汉明纠错与经典密码各走一遍（Crc/HammingBlock 用方法）。
STD_CHECK_SRCS := examples/stdcheck.c6 std/core/types.c6 std/text/str.c6 \
                  std/encoding/luhn.c6 std/encoding/crc16.c6 \
                  std/encoding/hamming.c6 std/encoding/classic.c6

std-check: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcheck $(STD_CHECK_SRCS)
	./build/stdcheck

# 标准库第五十九批（Linux 配置文本）：mounts / fstab / osrelease / systemd。
# 示例把 /proc/mounts、/etc/fstab、/etc/os-release 与 unit 文件各解析一遍。
STD_LINUX_SRCS := examples/stdlinux.c6 std/core/types.c6 std/text/str.c6 \
                  std/os/mounts.c6 std/os/fstab.c6 std/os/osrelease.c6 \
                  std/os/systemd.c6

std-linux: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdlinux $(STD_LINUX_SRCS)
	./build/stdlinux

# 标准库第六十批（二进制与数值工具）：binio / bitio / half / align。
# 示例把整数读写、按位读写、半精度转换与对齐工具各走一遍，链接带 -lm。
STD_BIN2_SRCS := examples/stdbin2.c6 std/core/types.c6 std/text/str.c6 \
                 std/core/binio.c6 std/core/bitio.c6 std/core/half.c6 \
                 std/core/align.c6

std-bin2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdbin2 $(STD_BIN2_SRCS) -lm
	./build/stdbin2

# 标准库第六十一批（文本清洗与标注）：sanitize / tabs / quote / nato。
# 示例把清洗、制表符展开还原、shell 引用与 NATO 拼读各走一遍（全部方法调用）。
STD_CLEAN_SRCS := examples/stdclean.c6 std/core/types.c6 std/text/str.c6 \
                  std/text/sanitize.c6 std/text/tabs.c6 std/text/quote.c6 \
                  std/text/nato.c6

std-clean: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdclean $(STD_CLEAN_SRCS)
	./build/stdclean

# 标准库第六十二批（颜色与单位）：color / palette / colorname / units。
# 示例把颜色转换、调色板、命名颜色与单位换算各走一遍（Color/Conv 用方法）。
STD_COLOR_SRCS := examples/stdcolor.c6 std/core/types.c6 std/text/str.c6 \
                  std/core/color.c6 std/data/palette.c6 \
                  std/text/colorname.c6 std/core/units.c6

std-color: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcolor $(STD_COLOR_SRCS)
	./build/stdcolor

# 标准库第六十三批（基础容器补充）：stack / queue / slots / bag。
# 示例把栈（括号匹配）、队列（回绕）、槽位分配与多重集合各走一遍（全部方法）。
STD_CONT_SRCS := examples/stdcontainers.c6 std/core/types.c6 std/text/str.c6 \
                 std/container/stack.c6 std/container/queue.c6 \
                 std/container/slots.c6 std/container/bag.c6

std-cont: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcontainers $(STD_CONT_SRCS)
	./build/stdcontainers

# 标准库第六十四批（统计小工具）：histogram / moving / series / quantile。
# 示例把直方图、滑动平均、序列统计与分位数各走一遍，链接带 -lm。
STD_STATS2_SRCS := examples/stdstats2.c6 std/core/types.c6 std/text/str.c6 \
                   std/core/histogram.c6 std/core/moving.c6 \
                   std/core/series.c6 std/core/quantile.c6

std-stats: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdstats2 $(STD_STATS2_SRCS) -lm
	./build/stdstats2

# 标准库第六十五批（字符串算法）：kmp / bmh / suffix / wordplay。
# 示例把 KMP/BMH 查找、后缀数组检索与回文/异位词各走一遍（方法调用）。
STD_SEARCH_SRCS := examples/stdsearch.c6 std/core/types.c6 std/text/str.c6 \
                   std/text/kmp.c6 std/text/bmh.c6 std/text/suffix.c6 \
                   std/text/wordplay.c6

std-search: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdsearch $(STD_SEARCH_SRCS)
	./build/stdsearch

# 标准库第六十六批（老音频格式头）：aiff / au / voc / ircam。
# 示例把四种格式的头与块结构各解析一遍（AIFF/VOC 用块游标）。
STD_AUDIO2_SRCS := examples/stdlegacy2.c6 std/core/types.c6 std/text/str.c6 \
                   std/data/aiff.c6 std/data/au.c6 std/data/voc.c6 \
                   std/data/ircam.c6

std-audio2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdlegacy2 $(STD_AUDIO2_SRCS)
	./build/stdlegacy2

# 标准库第六十七批（小型文本协议）：irc / gopher / whois / ident。
# 示例把 IRC 消息、Gopher 菜单、WHOIS 字段与 ident 端口对/应答各走一遍。
STD_PROTO3_SRCS := examples/stdnet2.c6 std/core/types.c6 std/text/str.c6 \
                   std/net/irc.c6 std/net/gopher.c6 std/net/whois.c6 \
                   std/net/ident.c6

std-proto3: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdnet2 $(STD_PROTO3_SRCS)
	./build/stdnet2

# 标准库第六十八批（轻量标记语言）：rst / org / tex / pod。
# 示例把四种标记的结构游标各走一遍（doc.next / doc.rewind）。
STD_MARKUP_SRCS := examples/stdmarkup.c6 std/core/types.c6 std/text/str.c6 \
                   std/text/rst.c6 std/text/org.c6 std/text/tex.c6 \
                   std/text/pod.c6

std-markup: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdmarkup $(STD_MARKUP_SRCS)
	./build/stdmarkup

# 标准库第六十九批（文本分析）：words / sentences / ngram / readability。
# 示例把切词、切句、n-gram 与可读性评分各走一遍（游标方法）。
STD_TEXTAN_SRCS := examples/stdtextan.c6 std/core/types.c6 std/text/str.c6 \
                   std/text/words.c6 std/text/sentences.c6 \
                   std/text/ngram.c6 std/text/readability.c6

std-textan: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdtextan $(STD_TEXTAN_SRCS)
	./build/stdtextan

# 标准库第七十批（系统配置文件）：dotenv / hosts / resolvconf / knownhosts。
# 示例把四种配置文件各解析一遍（游标方法）。
STD_CFG2_SRCS := examples/stdcfg.c6 std/core/types.c6 std/text/str.c6 \
                 std/data/dotenv.c6 std/data/hosts.c6 \
                 std/data/resolvconf.c6 std/data/knownhosts.c6

std-cfg2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdcfg $(STD_CFG2_SRCS)
	./build/stdcfg

# 标准库第七十一批（匹配与对齐）：aho / rabin / interval / align。
# 示例把多模式扫描、滚动哈希查找、区间集合与序列比对各走一遍。
STD_MATCH_SRCS := examples/stdmatch.c6 std/core/types.c6 std/text/str.c6 \
                  std/text/aho.c6 std/text/rabin.c6 \
                  std/container/interval.c6 std/text/align.c6

std-match: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdmatch $(STD_MATCH_SRCS)
	./build/stdmatch

# 标准库第七十二批（异步编程基础）：future / timer / task / reactor。
# 示例把 Future 回调与组合、定时器堆、协作式任务与 poll 事件循环各走一遍。
STD_ASYNC_SRCS := examples/stdasync.c6 std/core/types.c6 std/text/str.c6 \
                  std/core/future.c6 std/core/timer.c6 std/core/task.c6 \
                  std/os/poll.c6 std/os/reactor.c6

std-async: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdasync $(STD_ASYNC_SRCS)
	./build/stdasync

# 标准库第七十三批（基于 async 的上一层）：executor / asio / retry / awaitgroup。
# 示例把事件循环、按行异步读、退避重试与完成计数组各走一遍。
STD_ASYNC2_SRCS := examples/stdasync2.c6 std/core/types.c6 std/text/str.c6 \
                   std/core/future.c6 std/os/poll.c6 std/core/timer.c6 \
                   std/core/task.c6 std/os/reactor.c6 std/core/executor.c6 \
                   std/os/asio.c6 std/core/retry.c6 std/core/awaitgroup.c6

std-async2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdasync2 $(STD_ASYNC2_SRCS)
	./build/stdasync2

# 标准库第七十四批（数值方法与拟合）：solve / ode / optimize / least。
# 示例把求根、定步长积分、一维极值与最小二乘拟合各走一遍。
STD_MATH3_SRCS := examples/stdmath3.c6 std/core/types.c6 std/text/str.c6 \
                  std/core/solve.c6 std/core/ode.c6 \
                  std/core/optimize.c6 std/core/least.c6

std-math3: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdmath3 $(STD_MATH3_SRCS) -lm
	./build/stdmath3

# 标准库第七十五批（线性代数）：lu / eigen / tridiag / sparse。
# 示例把 LU 求解求逆、特征分解与幂法、三对角 Thomas、CSR 稀疏矩阵各走一遍。
STD_LINALG_SRCS := examples/stdlinalg.c6 std/core/types.c6 std/text/str.c6 \
                   std/core/lu.c6 std/core/eigen.c6 \
                   std/core/tridiag.c6 std/core/sparse.c6

std-linalg: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdlinalg $(STD_LINALG_SRCS) -lm
	./build/stdlinalg

# 标准库第七十六批（检索与关键词）：inverted / bm25 / keyword / stem。
# 示例把倒排索引查询、BM25 打分、词频 Top-N 与词干化各走一遍。
STD_SEARCH2_SRCS := examples/stdsearch2.c6 std/core/types.c6 std/text/str.c6 \
                    std/text/inverted.c6 std/text/bm25.c6 \
                    std/text/keyword.c6 std/text/stem.c6

std-search2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdsearch2 $(STD_SEARCH2_SRCS) -lm
	./build/stdsearch2

# 标准库第七十七批（图算法进阶）：mst / flow / scc / match。
# 示例把最小生成树、最大流、强连通分量与二分图匹配各走一遍。
STD_GRAPH2_SRCS := examples/stdgraph2.c6 std/core/types.c6 std/text/str.c6 \
                   std/container/mst.c6 std/container/flow.c6 \
                   std/container/scc.c6 std/container/match.c6

std-graph2: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdgraph2 $(STD_GRAPH2_SRCS)
	./build/stdgraph2

# 无 libc（freestanding）冒烟测试：不链接 crt / libc，入口是 _start
fstest: build
	./tcc/c6 -B tcc -static -nostdlib -o build/freestanding_fstr \
	    examples/freestanding_fstr.c6 tcc/libtcc1.a
	./build/freestanding_fstr

# wasm / 交叉目标：先编出目标运行库，再冒烟（需要 clang；跑起来需要 node）
#   make wasmrt    只构建 tcc/libtcc1-<arch>.a
#   make wasmtest  用 tcc --target=<triple> 出 .wasm，再用 node 跑一遍
LLVMCC      ?= clang
WASM_TRIPLE ?= wasm32-unknown-unknown

wasmrt: build
	LLVMCC=$(LLVMCC) ./scripts/build-wasm-runtime.sh $(WASM_TRIPLE)

wasmtest: build
	LLVMCC=$(LLVMCC) ./scripts/wasm-smoke.sh $(WASM_TRIPLE)

# freestanding + 程序自己实现的 pthread 层：spawn / thread
fstest-go: build
	./tcc/c6 -B tcc -c -o build/freestanding_pthread.o \
	    examples/freestanding_pthread.c
	./tcc/c6 -B tcc -static -nostdlib -o build/freestanding_go \
	    examples/freestanding_go.c6 build/freestanding_pthread.o tcc/libtcc1.a
	./build/freestanding_go

# 完全没有操作系统：程序里一个 syscall 都没有，靠 ud2 / 踩空报结果
fstest-noos: build
	./tcc/c6 -B tcc -static -nostdlib -o build/freestanding_noos \
	    examples/freestanding_noos.c6 tcc/libtcc1.a
	@./build/freestanding_noos 2>/dev/null; st=$$?; \
	 if [ $$st -eq 132 ]; then \
	     echo "fstest-noos: ok（全部断言通过，最后 ud2 陷阱）"; \
	 else \
	     echo "fstest-noos: FAILED（status $$st，失败位掩码见程序）"; exit 1; \
	 fi

# 再用 qemu 的用户态模拟器跑一遍：不用本机的加载器，也不用 libc
# （qemu-x86_64 自己实现 syscall；装了 qemu-user 才有这个命令）
fstest-qemu: build
	@command -v qemu-x86_64 >/dev/null || { \
	     echo "fstest-qemu: 没有 qemu-x86_64，跳过"; exit 0; }
	qemu-x86_64 ./build/freestanding_fstr
	qemu-x86_64 ./build/freestanding_go
	@qemu-x86_64 ./build/freestanding_noos >/dev/null 2>&1; st=$$?; \
	 if [ $$st -eq 132 ]; then \
	     echo "fstest-qemu: noos ok（ud2 陷阱）"; \
	 else \
	     echo "fstest-qemu: noos FAILED（status $$st）"; exit 1; \
	 fi

# 裸机：BIOS 引导扇区（examples/boot/boot.S，512 字节，自己切长模式）
# 把 c6 程序从磁盘读进来跑，没有内核、没有 BIOS 之外的任何东西。
# 需要 binutils 的 as/ld/objcopy（引导扇区那几十行 16 位汇编，tcc 不认
# .code16/.code32，没法自己编）和 qemu-system-x86_64。
fstest-bare: build
	@command -v qemu-system-x86_64 >/dev/null || { \
	     echo "fstest-bare: 没有 qemu-system-x86_64，跳过"; exit 0; }
	@command -v as >/dev/null || { \
	     echo "fstest-bare: 没有 binutils，跳过"; exit 0; }
	./examples/boot/build.sh

# 把 tests/*.c6 里的 test 块搬上裸机：每个文件配一层 libc 替身 + 自动生成的
# 入口，跑在同一个 512 字节引导器上（见 examples/boot/runtests.sh）
fstest-bare-tests: build
	@command -v qemu-system-x86_64 >/dev/null || { \
	     echo "fstest-bare-tests: 没有 qemu-system-x86_64，跳过"; exit 0; }
	@command -v as >/dev/null || { \
	     echo "fstest-bare-tests: 没有 binutils，跳过"; exit 0; }
	./examples/boot/runtests.sh

# 裸机上的 go：自己写的抢占式调度器（8259A + 8254 + 中断里的上下文切换），
# 两个没有让出语句的任务靠时钟抢占交错推进（examples/boot/{sched.plain.c,go_bare.c}）
fstest-bare-go: build
	@command -v qemu-system-x86_64 >/dev/null || { \
	     echo "fstest-bare-go: 没有 qemu-system-x86_64，跳过"; exit 0; }
	@command -v as >/dev/null || { \
	     echo "fstest-bare-go: 没有 binutils，跳过"; exit 0; }
	./examples/boot/build.sh examples/boot/go_bare.c6 examples/boot/sched.plain.c \
	    examples/boot/shim.plain.c examples/boot/io.c

# 裸机上的 spawn 句柄：h->join() 等到任务结束、h->value() 拿到返回值。
# 走的是 libtcc1 的每线程块 + sched.plain.c 里那 8 个 pthread 入口。
fstest-bare-handle: build
	@command -v qemu-system-x86_64 >/dev/null || { \
	     echo "fstest-bare-handle: 没有 qemu-system-x86_64，跳过"; exit 0; }
	@command -v as >/dev/null || { \
	     echo "fstest-bare-handle: 没有 binutils，跳过"; exit 0; }
	SMP=1 ./examples/boot/build.sh examples/boot/handle_bare.c6 \
	    examples/boot/sched.plain.c examples/boot/shim.plain.c \
	    examples/boot/io.c

fstest-bare-smp: build
	@command -v qemu-system-x86_64 >/dev/null || { \
	     echo "fstest-bare-smp: 没有 qemu-system-x86_64，跳过"; exit 0; }
	@command -v as >/dev/null || { \
	     echo "fstest-bare-smp: 没有 binutils，跳过"; exit 0; }
	@SMP=2 EXTRA_CFLAGS=-DC6_SMP_CPUS=2 ./examples/boot/build.sh \
	    examples/boot/smp_bare.c6 examples/boot/sched.plain.c \
	    examples/boot/shim.plain.c examples/boot/io.c
	@SMP=1 EXTRA_CFLAGS=-DC6_SMP_CPUS=1 ./examples/boot/build.sh \
	    examples/boot/smp_bare.c6 examples/boot/sched.plain.c \
	    examples/boot/shim.plain.c examples/boot/io.c

test-all: selftest test c6selfcheck stdtest std-demo std-tour std-kit std-box std-web std-net std-more std-next std-node std-form std-embed std-text std-num std-pack std-conf std-cli std-wire std-algo std-thread std-term std-client std-serve std-crypto std-fsx std-cipher std-data std-calc std-arch std-bin std-img std-enc std-gfx std-doc std-proto std-exec std-ser std-sys std-hash std-legacy std-font std-disk std-transcode std-audio std-proto2 std-ds std-words std-math2 std-time std-packet std-db std-hash2 std-ordered std-bwt std-subs std-creds std-geo std-pkg std-check std-linux std-bin2 std-clean std-color std-cont std-stats std-search std-audio2 std-proto3 std-markup std-textan std-cfg2 std-match std-async std-async2 std-math3 std-linalg std-search2 std-graph2 fstest fstest-go \
	  fstest-noos fstest-qemu \
	  fstest-bare fstest-bare-tests fstest-bare-go fstest-bare-handle \
	  fstest-bare-smp

# 完整验收：test-all 之外还有 C99 后端的两套用例（96 组各跑一遍）和三条
# 自举路线（c6 / c99+gcc / c99+tcc，每条都比对目标文件是否逐字节相同）。
# 这些目标过去只能手动跑，发布前用这一条把它们全部钉住。
#
# 标准库的七十七个目标在这里显式列出（它们已经在 test-all 里，列出来是为了让
# 「发布前这一条」的覆盖面一眼可见）：stdtest 是 std/ 下每个模块逐个跑
# test 块（个数以 stdtest 的输出为准），std-demo/tour/kit/box/web/net/more/next/node/form/embed/text/num/pack/conf/cli/wire/algo/thread/term/client/serve/crypto/fsx/cipher/data/calc/arch/bin/img
# 是三十批模块拼出来的集成示例。以后 test-all 增删标准库目标，也不会悄悄改变
# check-all 的语义。
check-all: test-all errcheck c99test c99tcc-test selfhost c99selfhost c99selfhost-tcc \
           stdtest std-demo std-tour std-kit std-box std-web std-net std-more std-next std-node std-form std-embed std-text std-num std-pack std-conf std-cli std-wire std-algo std-thread std-term std-client std-serve std-crypto std-fsx std-cipher std-data std-calc std-arch std-bin std-img std-enc std-gfx std-doc std-proto std-exec std-ser std-sys std-hash std-legacy std-font std-disk std-transcode std-audio std-proto2 std-ds std-words std-math2 std-time std-packet std-db std-hash2 std-ordered std-bwt std-subs std-creds std-geo std-pkg std-check std-linux std-bin2 std-clean std-color std-cont std-stats std-search std-audio2 std-proto3 std-markup std-textan std-cfg2 std-match std-async std-async2 std-math3 std-linalg std-search2 std-graph2

# C99 输出后端已内置在 tcc/c6 里：同一份二进制用 -c99（或 -o *.c、
# -o <目录>）产出 C 源码。保留这个目标只是为了兼容旧脚本。
c99: build
	@echo "c99: C99 输出已内置在 $(TCC_DIR)/c6，无需单独构建"

# C99 输出后端的端到端测试：与 selftest 相同的测试用例，但每个源文件先
# 由同一个 tcc 生成 C 源码，再由系统 gcc 编译运行。
c99test: build
	BACKEND=c99 ./$(SELFTEST)

# 与 c99test 完全同一批用例，只是编译生成代码的宿主编译器换成 tcc 自己：
# 生成的文件必须能被 tcc 编过（没有 GNU 的 alias 属性，方法名靠生成文件里
# 的转发函数导出，va_list 助手用私有名字，见 tcc/c99-gen.c）。
c99tcc-test: build
	BACKEND=c99 C99CC=$(CURDIR)/$(TCC_DIR)/c6 \
	    C99FLAGS="-B $(CURDIR)/$(TCC_DIR) -std=c99 -w" ./$(SELFTEST)

# c6 语言模式自检：tcc 自己的源码也必须满足 "变量必须初始化"，所以拿 c6
# 规则编译一遍自己的源码是这条规则最严格的回归测试（同时覆盖 C99 后端的
# 翻译路径）；编出来的编译器还要能跑。
# c6 只认 .c6 后缀，而 tcc 的源码必须保持 .c（上游 C），所以这里把 tcc.c
# 拷成 build/ 下的 .c6 再编：TU 的语言由顶层文件的后缀决定，#include 进来
# 的 .c/.h 跟着同一份规则走，检查范围和以前用 -std=c6 时完全一样。
# 编译时带 -DC6_EXT（和 make build 一致）：不带它，#ifdef C6_EXT 里的
# 扩展实现根本不会进语法检查，规则就只能管到编译器的一半。
# 翻译出来的 C 还要交给宿主编译器编译一遍：c6 模式下输出文件自带运行时
# 前导（里面 include 了宿主的头文件），声明块里的原型必须不能和它们冲突。
# 两步都不许有诊断输出：编译器自己的源码编出警告，说明前端或后端在正常
# 代码上判断错了（例如死代码里的跳转标签让 "函数可能没有返回值" 的检查
# 误报），这类输出只能当错误处理。
c6selfcheck: build
	@mkdir -p $(SELFHOST_C6)
	@cp $(TCC_DIR)/tcc.c $(SELFHOST_C6)/selfcheck.c6
	@out=`$(TCC_DIR)/c6 -B $(TCC_DIR) -I $(TCC_DIR) $(SELFCHECK_DEFS) -c $(SELFHOST_C6)/selfcheck.c6 -o $(SELFHOST_C6)/selfcheck.o 2>&1`; \
	if [ -n "$$out" ]; then \
	    printf '%s\n' "$$out" >&2; \
	    echo "c6selfcheck: 用 tcc 编译自身时不应有任何诊断输出（上面这些就是）" >&2; \
	    exit 1; \
	fi
	@out=`$(TCC_DIR)/c6 -B $(TCC_DIR) -I $(TCC_DIR) $(SELFCHECK_DEFS) -o $(SELFHOST_C6)/selfcheck.c $(SELFHOST_C6)/selfcheck.c6 2>&1`; \
	if [ -n "$$out" ]; then \
	    printf '%s\n' "$$out" >&2; \
	    echo "c6selfcheck: 用 tcc 生成 C 时不应有任何诊断输出（上面这些就是）" >&2; \
	    exit 1; \
	fi
	$(HOSTCC) -std=c99 -c -o $(SELFHOST_C6)/selfcheck-c99.o $(SELFHOST_C6)/selfcheck.c
	$(TCC_DIR)/c6 -B $(TCC_DIR) -o $(SELFHOST_C6)/selfcheck $(SELFHOST_C6)/selfcheck.o $(SELFHOST_LIBS)
	$(SELFHOST_C6)/selfcheck -v

# 自举（c6 模式）：tcc 用它自己的后端编译自己的源码，再用编出来的编译器
# 重编一遍。两次的目标文件必须逐字节相同（bootstrap fixpoint），最后用
# stage2 编一个程序跑通，确认它不只是"能启动"。
selfhost: build
	@mkdir -p $(SELFHOST_C6)
	@echo "c6 自举 1/4：$(TCC_DIR)/c6 编译自己的源码"
	$(TCC_DIR)/c6 -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C6)/stage1.o
	$(TCC_DIR)/c6 -B $(TCC_DIR) -o $(SELFHOST_C6)/tcc-stage1 $(SELFHOST_C6)/stage1.o $(SELFHOST_LIBS)
	@echo "c6 自举 2/4：stage1 重新编译同一份源码"
	$(SELFHOST_C6)/tcc-stage1 -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C6)/stage2.o
	@echo "c6 自举 3/4：比较两次的目标文件"
	@cmp $(SELFHOST_C6)/stage1.o $(SELFHOST_C6)/stage2.o && echo "  两次编译逐字节相同"
	$(SELFHOST_C6)/tcc-stage1 -B $(TCC_DIR) -o $(SELFHOST_C6)/tcc-stage2 $(SELFHOST_C6)/stage2.o $(SELFHOST_LIBS)
	@echo "c6 自举 4/4：stage2 编一个程序并运行"
	@printf '%s\n' '#include <stdio.h>' 'int main(void)' '{' '    printf("hello from the c6 self-hosted tcc\n");' '    return 0;' '}' > $(SELFHOST_C6)/hello.c
	$(SELFHOST_C6)/tcc-stage2 -B $(TCC_DIR) $(SELFHOST_C6)/hello.c -o $(SELFHOST_C6)/hello
	@$(SELFHOST_C6)/hello
	@echo "c6 自举完成：$(SELFHOST_C6)/tcc-stage2"

# 自举（C99 后端）：tcc 先把 tcc.c 翻成 C99 源码，宿主 gcc 把它编成 stage2
# 编译器；stage2 是正常的 x86-64 tcc，用它自己再编一次 tcc.c 得到 stage3，
# stage3 再编一次必须得到同样的目标文件（fixpoint）。
c99selfhost: build
	@mkdir -p $(SELFHOST_C99)
	@echo "c99 自举 1/4：tcc 的 C99 后端把 tcc.c 翻成 C"
	$(TCC_DIR)/c6 -B $(TCC_DIR) $(SELFHOST_DEFS) -c99 -o $(SELFHOST_C99)/tcc-c99.c $(TCC_DIR)/tcc.c
	@echo "c99 自举 2/4：宿主 $(HOSTCC) 编译生成的 C，得到 stage2 编译器"
	$(HOSTCC) -std=c99 -O0 -w -o $(SELFHOST_C99)/tcc-stage2 $(SELFHOST_C99)/tcc-c99.c $(SELFHOST_LIBS)
	@echo "c99 自举 3/4：stage2 编译 tcc.c 得到 stage3"
	$(SELFHOST_C99)/tcc-stage2 -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C99)/stage3.o
	$(SELFHOST_C99)/tcc-stage2 -B $(TCC_DIR) -o $(SELFHOST_C99)/tcc-stage3 $(SELFHOST_C99)/stage3.o $(SELFHOST_LIBS)
	@echo "c99 自举 4/4：stage3 再编一次并比较目标文件"
	$(SELFHOST_C99)/tcc-stage3 -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C99)/stage4.o
	@cmp $(SELFHOST_C99)/stage3.o $(SELFHOST_C99)/stage4.o && echo "  两次编译逐字节相同"
	@printf '%s\n' '#include <stdio.h>' 'int main(void)' '{' '    printf("hello from the c99 self-hosted tcc\n");' '    return 0;' '}' > $(SELFHOST_C99)/hello.c
	$(SELFHOST_C99)/tcc-stage3 -B $(TCC_DIR) $(SELFHOST_C99)/hello.c -o $(SELFHOST_C99)/hello
	@$(SELFHOST_C99)/hello
	@echo "c99 自举完成：$(SELFHOST_C99)/tcc-stage3"

# 自举（C99 后端，宿主也是 tcc）：整条链不碰 gcc —— tcc 把 tcc.c 翻成 C，
# 再亲手把这份 C 编成 stage2；stage2 重编 tcc.c 得到的目标文件必须和 tcc
# 自己编出来的逐字节相同（生成的文件只有按 tcc 的 C 方言也能编，才走得通：
# 没有 GNU 的 alias 属性 / 内建 __va_arg 这些差别都在 c99-gen.c 里绕开了）。
c99selfhost-tcc: build
	@mkdir -p $(SELFHOST_C99TCC)
	@echo "c99+tcc 自举 1/4：tcc 的 C99 后端把 tcc.c 翻成 C"
	$(TCC_DIR)/c6 -B $(TCC_DIR) $(SELFHOST_DEFS) -c99 -o $(SELFHOST_C99TCC)/tcc-c99.c $(TCC_DIR)/tcc.c
	@echo "c99+tcc 自举 2/4：同一个 tcc 编译生成的 C，得到 stage2 编译器"
	$(TCC_DIR)/c6 -B $(TCC_DIR) -o $(SELFHOST_C99TCC)/tcc-stage2 $(SELFHOST_C99TCC)/tcc-c99.c $(SELFHOST_LIBS)
	@echo "c99+tcc 自举 3/4：stage2 编一遍 tcc.c，tcc 自己也编一遍"
	$(SELFHOST_C99TCC)/tcc-stage2 -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C99TCC)/stage3.o
	$(TCC_DIR)/c6 -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C99TCC)/native.o
	@echo "c99+tcc 自举 4/4：比较两个目标文件"
	@cmp $(SELFHOST_C99TCC)/native.o $(SELFHOST_C99TCC)/stage3.o && echo "  逐字节相同"
	@printf '%s\n' '#include <stdio.h>' 'int main(void)' '{' '    printf("hello from the tcc-hosted c99 self-hosted tcc\n");' '    return 0;' '}' > $(SELFHOST_C99TCC)/hello.c
	$(SELFHOST_C99TCC)/tcc-stage2 -B $(TCC_DIR) $(SELFHOST_C99TCC)/hello.c -o $(SELFHOST_C99TCC)/hello
	@$(SELFHOST_C99TCC)/hello
	@echo "c99+tcc 自举完成：$(SELFHOST_C99TCC)/tcc-stage2"

clean:
	rm -rf $(SELFHOST)
	rm -f $(TCC_DIR)/libtcc1-*.a
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) clean || true

distclean:
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) distclean || true

# --- help begin
# c6 —— 带语言扩展的 TinyCC
#
# 用法：make <目标>。提交/发布前跑 make check-all。
# 编译器选项、环境变量、每个目标的细节见 docs/reference/cli.md。
#
# 构建
#   bootstrap        首次配置 tcc/（已有 config.mak 就跳过，不联网）
#   build            扩展模式构建（EXTRA-DEFS=-DC6_EXT，产物 tcc/c6）
#   build-plain      上游模式构建（不定义 C6_EXT）
#   check-tcc        检查 tcc/ 源码是否完整
#   mode-ext         切到扩展模式（模式变化时自动清理旧产物）
#   mode-plain       切到上游模式
#   clean            清理构建产物与自举产物
#   distclean        连 configure 结果一起清理（不删源码）
#
# 测试与回归
#   test             上游 tests2 全量（扩展模式）
#   test-plain       关闭扩展重建后跑同一套上游测试
#   selftest         tests/ 下全部 c6 自测（test 块 + 编译期诊断）
#   errcheck         errbase.def 本身与 C126–C128 的检查
#   test-all         selftest + test + c6selfcheck + 标准库 + freestanding/裸机
#   check-all        提交/发布前跑这一条（test-all + errcheck + C99 后端 + 三条自举
#                    + 标准库的 stdtest/std-demo/tour/kit/box/web/net/more/next/node/form/embed/text/num/pack/conf/cli/wire/algo/thread/term/client/serve/crypto/fsx/cipher/data/calc/arch/bin/img）
#
# 标准库
#   stdtest          std/ 下每个模块的 test 块
#   std-demo         编译并运行 examples/wordcount.c6
#   std-tour         编译并运行 examples/stdtour.c6
#   std-kit          编译并运行 examples/stdkit.c6（第三批）
#   std-box          编译并运行 examples/stdbox.c6（第四批）
#   std-web          编译并运行 examples/stdweb.c6（第五批）
#   std-net          编译并运行 examples/stdnet.c6（第六批）
#   std-more         编译并运行 examples/stdmore.c6（第七批）
#   std-next         编译并运行 examples/stdnext.c6（第八批）
#   std-node         编译并运行 examples/stdnode.c6（第九批）
#   std-form         编译并运行 examples/stdform.c6（第十批）
#   std-embed        编译并运行 examples/stdembed.c6（第十一批，嵌入式基础）
#   std-text         编译并运行 examples/stdtext.c6（第十二批，文本算法）
#   std-num          编译并运行 examples/stdnum.c6（第十三批，数值类型）
#   std-pack         编译并运行 examples/stdpack.c6（第十四批，压缩与文本编码）
#   std-conf         编译并运行 examples/stdconf.c6（第十五批，配置与文档格式）
#   std-cli          编译并运行 examples/stdcli.c6（第十六批，命令行工具基础）
#   std-wire         编译并运行 examples/stdwire.c6（第十七批，网络协议基础）
#   std-algo         编译并运行 examples/stdalgo.c6（第十八批，算法与容器）
#   std-thread       编译并运行 examples/stdthread.c6（第十九批，并发与同步）
#   std-term         编译并运行 examples/stdterm.c6（第二十批，终端与进程控制）
#   std-client       编译并运行 examples/stdclient.c6（第二十一批，网络客户端）
#   std-serve        编译并运行 examples/stdserve.c6（第二十二批，Web 服务端）
#   std-crypto       编译并运行 examples/stdcrypto.c6（第二十三批，摘要与密钥派生）
#   std-fsx          编译并运行 examples/stdfsx.c6（第二十四批，文件系统进阶）
#   std-cipher       编译并运行 examples/stdcipher.c6（第二十五批，对称加密基础）
#   std-data         编译并运行 examples/stddata.c6（第二十六批，数据格式）
#   std-calc         编译并运行 examples/stdcalc.c6（第二十七批，数值工具）
#   std-arch         编译并运行 examples/stdarch.c6（第二十八批，压缩与归档）
#   std-bin          编译并运行 examples/stdbin.c6（第二十九批，二进制文件格式）
#   std-img          编译并运行 examples/stdimg.c6（第三十批，图像解码）
#   std-enc          编译并运行 examples/stdenc.c6（第三十一批，编码与凭据）
#   std-gfx          编译并运行 examples/stdgfx.c6（第三十二批，图像与 LZW）
#   std-doc          编译并运行 examples/stddoc.c6（第三十三批，结构化文本格式）
#   std-proto        编译并运行 examples/stdproto.c6（第三十四批，网络协议编解码）
#   std-exec         编译并运行 examples/stdexec.c6（第三十五批，二进制容器格式）
#   std-ser          编译并运行 examples/stdser.c6（第三十六批，序列化与标记）
#   std-sys          编译并运行 examples/stdsys.c6（第三十七批，系统视图）
#   std-hash         编译并运行 examples/stdhash.c6（第三十八批，哈希与校验）
#   std-legacy       编译并运行 examples/stdlegacy.c6（第三十九批，老图形格式）
#   std-font         编译并运行 examples/stdfont.c6（第四十批，字体与矢量）
#   std-disk         编译并运行 examples/stddisk.c6（第四十一批，分区与文件系统）
#   std-transcode    编译并运行 examples/stdtranscode.c6（第四十二批，老式文本编码）
#   std-audio        编译并运行 examples/stdaudio.c6（第四十三批，音频容器）
#   std-proto2       编译并运行 examples/stdproto2.c6（第四十四批，文本行协议）
#   std-ds           编译并运行 examples/stdstruct.c6（第四十五批，基础数据结构）
#   std-words        编译并运行 examples/stdwords.c6（第四十六批，文本变换）
#   std-math2        编译并运行 examples/stdnumeric.c6（第四十七批，数值工具）
#   std-time         编译并运行 examples/stdtime.c6（第四十八批，时间工具）
#   std-packet       编译并运行 examples/stdpacket.c6（第四十九批，网络报文）
#   std-db           编译并运行 examples/stddb.c6（第五十批，日志与数据库）
#   std-hash2        编译并运行 examples/stdhash2.c6（第五十一批，非加密散列补齐）
#   std-ordered      编译并运行 examples/stdordered.c6（第五十二批，有序容器）
#   std-bwt          编译并运行 examples/stdcmp.c6（第五十三批，变换式压缩积木）
#   std-subs         编译并运行 examples/stdsubs.c6（第五十四批，字幕与播放列表）
#   std-creds        编译并运行 examples/stdcreds.c6（第五十五批，凭据与配置）
#   std-geo          编译并运行 examples/stdgeo.c6（第五十六批，地理编码）
#   std-pkg          编译并运行 examples/stdpkg.c6（第五十七批，目标文件与包）
#   std-check        编译并运行 examples/stdcheck.c6（第五十八批，校验与纠错）
#   std-linux        编译并运行 examples/stdlinux.c6（第五十九批，Linux 配置文本）
#   std-bin2         编译并运行 examples/stdbin2.c6（第六十批，二进制与数值工具）
#   std-clean        编译并运行 examples/stdclean.c6（第六十一批，文本清洗与标注）
#   std-color        编译并运行 examples/stdcolor.c6（第六十二批，颜色与单位）
#   std-cont         编译并运行 examples/stdcontainers.c6（第六十三批，基础容器补充）
#   std-stats        编译并运行 examples/stdstats2.c6（第六十四批，统计小工具）
#   std-search       编译并运行 examples/stdsearch.c6（第六十五批，字符串算法）
#   std-audio2       编译并运行 examples/stdlegacy2.c6（第六十六批，老音频格式头）
#   std-proto3       编译并运行 examples/stdnet2.c6（第六十七批，小型文本协议）
#   std-markup       编译并运行 examples/stdmarkup.c6（第六十八批，轻量标记语言）
#   std-textan       编译并运行 examples/stdtextan.c6（第六十九批，文本分析）
#   std-cfg2         编译并运行 examples/stdcfg.c6（第七十批，系统配置文件）
#   std-match        编译并运行 examples/stdmatch.c6（第七十一批，匹配与对齐）
#   std-async        编译并运行 examples/stdasync.c6（第七十二批，异步编程基础）
#   std-async2       编译并运行 examples/stdasync2.c6（第七十三批，async 之上再盖一层）
#   std-math3        编译并运行 examples/stdmath3.c6（第七十四批，数值方法与拟合）
#   std-linalg       编译并运行 examples/stdlinalg.c6（第七十五批，线性代数）
#   std-search2      编译并运行 examples/stdsearch2.c6（第七十六批，检索与关键词）
#   std-graph2       编译并运行 examples/stdgraph2.c6（第七十七批，图算法进阶）
#
# freestanding、裸机与交叉目标
#   fstest                   无 libc 冒烟（不链接 crt/libc，入口 _start）
#   fstest-go                freestanding + 程序自己实现的 pthread 层（spawn/thread）
#   fstest-noos              完全没有操作系统（程序里一个 syscall 都没有）
#   fstest-qemu              用 qemu-x86_64 用户态模拟器跑（需要 qemu-user）
#   fstest-bare              512 字节 BIOS 引导扇区，在 qemu 空机器上启动
#   fstest-bare-tests        把 tests/*.c6 的 test 块搬上裸机跑
#   fstest-bare-go           裸机上的 go（自己写的抢占式调度器）
#   fstest-bare-handle       裸机上的 spawn 句柄（join/value）
#   fstest-bare-smp          裸机 SMP（2 核与 1 核各跑一遍）
#   wasmrt                   编目标平台运行库 tcc/libtcc1-<arch>.a（需要 clang）
#   wasmtest                 --target=<triple> 出 .wasm，再用 node 跑
#
# C99 输出后端
#   c99              兼容旧脚本的占位目标（C99 后端已内置，用 -c99）
#   c99test          与 selftest 同一套用例，经 C 后端后由 gcc 编译运行
#   c99tcc-test      同上，宿主编译器换成 tcc/c6
#
# 自举
#   c6selfcheck      tcc.c 拷成 .c6 用 c6 规则编译（初始化规则最严格的回归）
#   selfhost         c6 路线：tcc -> stage1 -> stage2，目标文件逐字节比对
#   c99selfhost      C99 路线：生成 C -> gcc -> stage2 -> stage3 -> 比对
#   c99selfhost-tcc  同上，但生成的 C 也由 tcc 自己编（整条链不需要 gcc）
#
# 其它
#   help             打印这份清单
# --- help end
help:
	@sed -n '/^# --- help begin/,/^# --- help end/p' $(MAKEFILE_LIST) \
	    | sed -e '1d' -e '$$d' -e 's/^# \{0,1\}//'
