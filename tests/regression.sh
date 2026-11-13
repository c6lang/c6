#!/bin/sh
# c6 回归用例执行器。用例逻辑从原根 Makefile 迁来，已去掉全部 make 语法。
# 调度、并发、日志、超时、过滤由 CTest 负责（见 cmake/C6Testing.cmake）：
#   ctest --test-dir build -R '^std-demo$' --output-on-failure
#   ctest --test-dir build -L regression -j 8 --output-on-failure
# 也可以单独调用：tests/regression.sh <用例名>。
#
# 环境变量（由 CTest 的 ENVIRONMENT 属性注入，均有默认值）：
#   C6_TCC_DIR  C6_MAKE  C6_SELFTEST  C6_ERRBASE  C6_HOSTCC  C6_CFLAGS
#   C6_SELFHOST  C6_BARE  C6_LLVMCC  C6_NODE  C6_WASM_TRIPLE  C6_TMP
set -eu
C6_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$C6_ROOT"
# CTest 给每个用例注入 C6_CASE_OUT（build/tests/<用例>）作为独立输出目录，
# 互不干扰；单独调用时退回原来的 build/。被测 c6 程序内部还有写死相对 CWD
# 的 build/... 路径（std 模块的 test 块与 4 个 examples 的 main），C6_OUT
# 覆盖不了它们；CTest 给 stdtest/std-kit/std-client/std-serve/std-fsx 加了
# 共享 RESOURCE_LOCK 串行，见 cmake/C6Testing.cmake。构建目录路径不能含空格。
C6_OUT=${C6_CASE_OUT:-build}
: "${C6_TCC_DIR:=tcc}"
: "${C6_MAKE:=make}"
: "${C6_SELFTEST:=tests/run-tests.sh}"
: "${C6_ERRBASE:=tests/run-errbase.sh}"
: "${C6_HOSTCC:=gcc}"
: "${C6_CFLAGS:=-g -O0 -Wall -Wno-unused-result}"
: "${C6_SELFHOST:=$C6_OUT/selfhost}"
: "${C6_BARE:=$C6_OUT/bare}"
: "${C6_LLVMCC:=clang}"
: "${C6_NODE:=node}"
: "${C6_WASM_TRIPLE:=wasm32-unknown-unknown}"
: "${C6_TMP:=${TMPDIR:-/tmp}}"
# 用例各自拥有一整套输出目录，不需要靠串行规避路径冲突。
mkdir -p "$C6_OUT" "$C6_SELFHOST" "$C6_BARE" "$C6_TMP"

# 下面三行既是文档，也是 CTest 标签 / 互斥组的单一数据源，不要改名。

C6_STD_TARGETS="stdtest std-demo std-tour std-kit std-box std-web std-net std-more std-next std-node std-form std-embed std-text std-num std-pack std-conf std-cli std-wire std-algo std-thread std-term std-client std-serve std-crypto std-fsx std-cipher std-data std-calc std-arch std-bin std-img std-enc std-gfx std-doc std-proto std-exec std-ser std-sys std-hash std-legacy std-font std-disk std-transcode std-audio std-proto2 std-ds std-words std-math2 std-time std-packet std-db std-hash2 std-ordered std-bwt std-subs std-creds std-geo std-pkg std-check std-linux std-bin2 std-clean std-color std-cont std-stats std-search std-audio2 std-proto3 std-markup std-textan std-cfg2 std-match std-async std-async2 std-math3 std-linalg std-search2 std-graph2 std-async3 std-geo2 std-async4 std-mesh std-https std-async5 std-ub std-sec std-mw std-proto4 std-ops std-docs std-mgmt std-util"
C6_CHECK_PARALLEL="selftest errcheck c99test c99tcc-test c6selfcheck selfhost c99selfhost c99selfhost-tcc test fstest fstest-go fstest-noos fstest-qemu fstest-bare fstest-bare-tests fstest-bare-go fstest-bare-handle fstest-bare-smp"
C6_TEST_ALL="selftest test c6selfcheck stdtest std-demo std-tour std-kit std-box std-web std-net std-more std-next std-node std-form std-embed std-text std-num std-pack std-conf std-cli std-wire std-algo std-thread std-term std-client std-serve std-crypto std-fsx std-cipher std-data std-calc std-arch std-bin std-img std-enc std-gfx std-doc std-proto std-exec std-ser std-sys std-hash std-legacy std-font std-disk std-transcode std-audio std-proto2 std-ds std-words std-math2 std-time std-packet std-db std-hash2 std-ordered std-bwt std-subs std-creds std-geo std-pkg std-check std-linux std-bin2 std-clean std-color std-cont std-stats std-search std-audio2 std-proto3 std-markup std-textan std-cfg2 std-match std-async std-async2 std-math3 std-linalg std-search2 std-graph2 std-async3 std-geo2 std-async4 std-mesh std-https std-async5 std-ub std-sec std-mw std-proto4 std-ops std-docs std-mgmt std-util fstest fstest-go fstest-noos fstest-qemu fstest-bare fstest-bare-tests fstest-bare-go fstest-bare-handle fstest-bare-smp"

case "${1:-}" in
std-demo)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/wordcount examples/wordcount.c6 examples/wordcount/text.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/container/sort.c6 std/container/vec.c6 std/container/span.c6 std/text/fmt.c6 std/container/range.c6
  $C6_OUT/wordcount
  ;;

std-tour)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdtour examples/stdtour.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/container/span.c6 std/text/fmt.c6 std/text/strbuf.c6 std/text/utf8.c6 std/core/bits.c6 std/core/hash.c6 std/os/path.c6 std/container/deque.c6
  $C6_OUT/stdtour
  ;;

std-kit)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdkit examples/stdkit.c6 std/core/types.c6 std/core/err.c6 std/container/map.c6 std/container/set.c6 std/text/ascii.c6 std/text/num.c6 std/encoding/codec.c6 std/core/rand.c6 std/os/fs.c6
  $C6_OUT/stdkit
  ;;

std-box)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdbox examples/stdbox.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/core/mem.c6 std/text/num.c6 std/data/json.c6 std/core/bits.c6 std/encoding/sha256.c6 std/data/csv.c6 std/os/env.c6
  $C6_OUT/stdbox
  ;;

std-web)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdweb examples/stdweb.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/os/path.c6 std/encoding/base64.c6 std/net/url.c6 std/core/rand.c6 std/os/time.c6 std/encoding/uuid.c6 std/net/mime.c6
  $C6_OUT/stdweb
  ;;

std-net)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdnet examples/stdnet.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/container/sort.c6 std/net/inet.c6 std/encoding/bitbuf.c6 std/container/pqueue.c6 std/data/ini.c6
  $C6_OUT/stdnet
  ;;

std-more)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdmore examples/stdmore.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/core/bits.c6 std/container/bitset.c6 std/text/glob.c6 std/encoding/base32.c6 std/net/http.c6
  $C6_OUT/stdmore
  ;;

std-next)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdnext examples/stdnext.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/encoding/base58.c6 std/encoding/varint.c6 std/text/semver.c6 std/net/cookie.c6
  $C6_OUT/stdnext
  ;;

std-node)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdnode examples/stdnode.c6 std/core/types.c6 std/core/err.c6 std/container/list.c6 std/container/ring.c6 std/text/utf16.c6 std/text/ansi.c6
  $C6_OUT/stdnode
  ;;

std-form)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdform examples/stdform.c6 std/core/types.c6 std/core/err.c6 std/text/utf8.c6 std/data/props.c6 std/net/multipart.c6 std/encoding/punycode.c6 std/text/width.c6
  $C6_OUT/stdform
  ;;

std-embed)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdembed examples/stdembed.c6 std/core/types.c6 std/core/err.c6 std/core/crc.c6 std/core/fixed.c6 std/core/pool.c6 std/encoding/frame.c6
  $C6_OUT/stdembed
  ;;

std-text)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdtext examples/stdtext.c6 std/core/types.c6 std/core/mem.c6 std/text/regex.c6 std/text/utf8.c6 std/text/diff.c6 std/text/fuzzy.c6 std/text/wrap.c6 std/text/width.c6
  $C6_OUT/stdtext
  ;;

std-num)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdnum examples/stdnum.c6 std/core/types.c6 std/core/math.c6 std/core/bignum.c6 std/core/decimal.c6 std/core/rational.c6 std/core/complex.c6
  $C6_OUT/stdnum
  ;;

std-pack)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdpack examples/stdpack.c6 std/core/types.c6 std/encoding/lz4.c6 std/encoding/rle.c6 std/encoding/base85.c6 std/encoding/hexdump.c6
  $C6_OUT/stdpack
  ;;

std-conf)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdconf examples/stdconf.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/text/num.c6 std/data/toml.c6 std/data/xml.c6 std/data/query.c6 std/text/template.c6
  $C6_OUT/stdconf
  ;;

std-cli)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcli examples/stdcli.c6 std/core/types.c6 std/text/str.c6 std/text/utf8.c6 std/text/width.c6 std/os/args.c6 std/text/table.c6 std/text/natural.c6 std/os/proc.c6
  $C6_OUT/stdcli
  ;;

std-wire)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdwire examples/stdwire.c6 std/core/types.c6 std/core/err.c6 std/encoding/base64.c6 std/encoding/sha1.c6 std/net/ws.c6 std/net/dns.c6 std/net/tcp.c6
  $C6_OUT/stdwire
  ;;

std-algo)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdalgo examples/stdalgo.c6 std/core/types.c6 std/container/graph.c6 std/container/lru.c6 std/container/trie.c6 std/container/region.c6
  $C6_OUT/stdalgo
  ;;

std-thread)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdthread examples/stdthread.c6 std/core/types.c6 std/core/atomic.c6 std/os/sync.c6 std/os/thread.c6 std/container/chan.c6
  $C6_OUT/stdthread
  ;;

std-term)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdterm examples/stdterm.c6 std/core/types.c6 std/os/tty.c6 std/os/signal.c6 std/os/pipe.c6 std/os/poll.c6
  $C6_OUT/stdterm
  ;;

std-client)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdclient examples/stdclient.c6 std/core/types.c6 std/core/err.c6 std/net/udp.c6 std/net/uds.c6 std/net/dns.c6 std/net/resolver.c6 std/os/io.c6 std/text/str.c6 std/net/tcp.c6 std/net/url.c6 std/net/httpc.c6
  $C6_OUT/stdclient
  ;;

std-serve)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdserve examples/stdserve.c6 std/core/types.c6 std/core/err.c6 std/net/httpd.c6 std/net/http.c6 std/net/tcp.c6 std/net/router.c6 std/net/assets.c6 std/net/mime.c6 std/net/url.c6 std/os/fs.c6 std/os/io.c6 std/os/path.c6 std/text/str.c6 std/net/session.c6 std/core/rand.c6 std/os/time.c6 std/net/cookie.c6 std/net/httpc.c6
  $C6_OUT/stdserve
  ;;

std-crypto)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcrypto examples/stdcrypto.c6 std/core/types.c6 std/core/err.c6 std/core/bits.c6 std/encoding/md5.c6 std/encoding/sha1.c6 std/encoding/sha256.c6 std/encoding/sha512.c6 std/encoding/hkdf.c6 std/encoding/pbkdf2.c6 std/os/time.c6
  $C6_OUT/stdcrypto
  ;;

std-fsx)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdfsx examples/stdfsx.c6 std/core/types.c6 std/core/err.c6 std/os/walk.c6 std/os/temp.c6 std/os/lock.c6 std/os/mmap.c6 std/os/fs.c6 std/os/path.c6 std/os/time.c6 std/text/str.c6
  $C6_OUT/stdfsx
  ;;

std-cipher)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcipher examples/stdcipher.c6 std/core/types.c6 std/core/bits.c6 std/encoding/aes.c6 std/encoding/chacha20.c6 std/encoding/poly1305.c6 std/encoding/random.c6
  $C6_OUT/stdcipher
  ;;

std-data)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stddata examples/stddata.c6 std/core/types.c6 std/text/str.c6 std/data/bencode.c6 std/data/msgpack.c6 std/data/cbor.c6 std/data/pem.c6 std/encoding/base64.c6
  $C6_OUT/stddata
  ;;

std-calc)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcalc examples/stdcalc.c6 std/core/types.c6 std/core/geometry.c6 std/core/interp.c6 std/core/stats.c6 std/core/matrix.c6
  $C6_OUT/stdcalc
  ;;

std-arch)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdarch examples/stdarch.c6 std/core/types.c6 std/core/crc.c6 std/encoding/deflate.c6 std/encoding/zlib.c6 std/encoding/gzip.c6 std/encoding/tar.c6 std/text/str.c6
  $C6_OUT/stdarch
  ;;

std-bin)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdbin examples/stdbin.c6 std/core/types.c6 std/core/crc.c6 std/encoding/deflate.c6 std/encoding/zlib.c6 std/data/wav.c6 std/data/png.c6 std/data/elf.c6 std/data/zip.c6 std/text/str.c6
  $C6_OUT/stdbin
  ;;

std-img)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdimg examples/stdimg.c6 std/core/types.c6 std/data/bmp.c6 std/data/tga.c6 std/data/qoi.c6 std/data/pnm.c6
  $C6_OUT/stdimg
  ;;

std-enc)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdenc examples/stdenc.c6 std/core/types.c6 std/core/bits.c6 std/text/str.c6 std/encoding/asn1.c6 std/encoding/base64.c6 std/encoding/sha1.c6 std/encoding/sha256.c6 std/encoding/sha512.c6 std/encoding/otp.c6 std/data/x509.c6 std/data/jwt.c6
  $C6_OUT/stdenc
  ;;

std-gfx)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdgfx examples/stdgfx.c6 std/core/types.c6 std/core/crc.c6 std/text/str.c6 std/encoding/lzw.c6 std/encoding/zlib.c6 std/encoding/deflate.c6 std/encoding/rle.c6 std/data/png.c6 std/data/gif.c6 std/data/tiff.c6 std/data/ico.c6
  $C6_OUT/stdgfx
  ;;

std-doc)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stddoc examples/stddoc.c6 std/core/types.c6 std/text/str.c6 std/data/html.c6 std/data/yaml.c6 std/data/vcf.c6 std/data/ics.c6
  $C6_OUT/stddoc
  ;;

std-proto)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdproto examples/stdproto.c6 std/core/types.c6 std/core/err.c6 std/text/str.c6 std/net/inet.c6 std/net/sse.c6 std/net/socks5.c6 std/net/mqtt.c6 std/encoding/snappy.c6
  $C6_OUT/stdproto
  ;;

std-exec)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdexec examples/stdexec.c6 std/core/types.c6 std/text/str.c6 std/data/cpio.c6 std/data/pe.c6 std/data/macho.c6 std/data/wasm.c6
  $C6_OUT/stdexec
  ;;

std-ser)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdser examples/stdser.c6 std/core/types.c6 std/text/str.c6 std/data/bson.c6 std/data/ubjson.c6 std/data/plist.c6 std/text/markdown.c6
  $C6_OUT/stdser
  ;;

std-sys)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdsys examples/stdsys.c6 std/core/types.c6 std/text/str.c6 std/os/sysinfo.c6 std/os/resource.c6 std/os/user.c6 std/os/pty.c6
  $C6_OUT/stdsys
  ;;

std-hash)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdhash examples/stdhash.c6 std/core/types.c6 std/text/str.c6 std/encoding/sha3.c6 std/encoding/blake2s.c6 std/encoding/crc32c.c6 std/encoding/xxhash.c6
  $C6_OUT/stdhash
  ;;

std-legacy)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdlegacy examples/stdlegacy.c6 std/core/types.c6 std/text/str.c6 std/data/pcx.c6 std/data/hdr.c6 std/data/xbm.c6 std/data/xpm.c6
  $C6_OUT/stdlegacy
  ;;

std-font)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdfont examples/stdfont.c6 std/core/types.c6 std/text/str.c6 std/data/bdf.c6 std/data/woff.c6 std/data/svg.c6 std/data/ttf.c6 -lm
  $C6_OUT/stdfont
  ;;

std-disk)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stddisk examples/stddisk.c6 std/core/types.c6 std/text/str.c6 std/data/mbr.c6 std/data/fat.c6 std/data/iso.c6 std/data/ext2.c6
  $C6_OUT/stddisk
  ;;

std-transcode)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdtranscode examples/stdtranscode.c6 std/core/types.c6 std/text/str.c6 std/encoding/base91.c6 std/encoding/uu.c6 std/encoding/yenc.c6 std/encoding/ebcdic.c6
  $C6_OUT/stdtranscode
  ;;

std-audio)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdaudio examples/stdaudio.c6 std/core/types.c6 std/text/str.c6 std/data/mp3.c6 std/data/ogg.c6 std/data/flac.c6 std/data/midi.c6
  $C6_OUT/stdaudio
  ;;

std-proto2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdproto2 examples/stdproto2.c6 std/core/types.c6 std/text/str.c6 std/net/smtp.c6 std/net/ftp.c6 std/net/redis.c6 std/net/telnet.c6
  $C6_OUT/stdproto2
  ;;

std-ds)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdstruct examples/stdstruct.c6 std/core/types.c6 std/text/str.c6 std/container/fenwick.c6 std/container/segtree.c6 std/container/dsu.c6 std/container/bloom.c6
  $C6_OUT/stdstruct
  ;;

std-words)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdwords examples/stdwords.c6 std/core/types.c6 std/text/str.c6 std/text/slug.c6 std/text/casing.c6 std/text/roman.c6 std/text/phonetic.c6
  $C6_OUT/stdwords
  ;;

std-math2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdnumeric examples/stdnumeric.c6 std/core/types.c6 std/core/vector.c6 std/core/polynomial.c6 std/core/fft.c6 std/core/spline.c6 -lm
  $C6_OUT/stdnumeric
  ;;

std-time)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdtime examples/stdtime.c6 std/core/types.c6 std/text/str.c6 std/os/calendar.c6 std/os/iso8601.c6 std/os/duration.c6 std/os/cron.c6
  $C6_OUT/stdtime
  ;;

std-packet)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdpacket examples/stdpacket.c6 std/core/types.c6 std/text/str.c6 std/net/ntp.c6 std/net/syslog.c6 std/net/stun.c6 std/net/tftp.c6
  $C6_OUT/stdpacket
  ;;

std-db)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stddb examples/stddb.c6 std/core/types.c6 std/text/str.c6 std/os/log.c6 std/data/sqlite.c6 std/net/postgres.c6 std/net/mysql.c6
  $C6_OUT/stddb
  ;;

std-hash2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdhash2 examples/stdhash2.c6 std/core/types.c6 std/text/str.c6 std/encoding/fnv.c6 std/encoding/adler32.c6 std/encoding/murmur3.c6 std/encoding/siphash.c6
  $C6_OUT/stdhash2
  ;;

std-ordered)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdordered examples/stdordered.c6 std/core/types.c6 std/text/str.c6 std/container/treap.c6 std/container/heap.c6 std/container/skiplist.c6 std/container/counter.c6
  $C6_OUT/stdordered
  ;;

std-bwt)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcmp examples/stdcmp.c6 std/core/types.c6 std/text/str.c6 std/encoding/bwt.c6 std/encoding/mtf.c6 std/encoding/delta.c6 std/encoding/huffman.c6
  $C6_OUT/stdcmp
  ;;

std-subs)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdsubs examples/stdsubs.c6 std/core/types.c6 std/text/str.c6 std/data/srt.c6 std/data/ass.c6 std/data/lrc.c6 std/data/m3u.c6
  $C6_OUT/stdsubs
  ;;

std-creds)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcreds examples/stdcreds.c6 std/core/types.c6 std/text/str.c6 std/data/netrc.c6 std/data/sshconfig.c6 std/data/keys.c6 std/data/crontab.c6
  $C6_OUT/stdcreds
  ;;

std-geo)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdgeo examples/stdgeo.c6 std/core/types.c6 std/text/str.c6 std/core/geo.c6 std/data/geohash.c6 std/data/maidenhead.c6 std/data/dms.c6 -lm
  $C6_OUT/stdgeo
  ;;

std-pkg)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdpkg examples/stdpkg.c6 std/core/types.c6 std/text/str.c6 std/data/ar.c6 std/data/coff.c6 std/data/class.c6 std/data/rpm.c6
  $C6_OUT/stdpkg
  ;;

std-check)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcheck examples/stdcheck.c6 std/core/types.c6 std/text/str.c6 std/encoding/luhn.c6 std/encoding/crc16.c6 std/encoding/hamming.c6 std/encoding/classic.c6
  $C6_OUT/stdcheck
  ;;

std-linux)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdlinux examples/stdlinux.c6 std/core/types.c6 std/text/str.c6 std/os/mounts.c6 std/os/fstab.c6 std/os/osrelease.c6 std/os/systemd.c6
  $C6_OUT/stdlinux
  ;;

std-bin2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdbin2 examples/stdbin2.c6 std/core/types.c6 std/text/str.c6 std/core/binio.c6 std/core/bitio.c6 std/core/half.c6 std/core/align.c6 -lm
  $C6_OUT/stdbin2
  ;;

std-clean)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdclean examples/stdclean.c6 std/core/types.c6 std/text/str.c6 std/text/sanitize.c6 std/text/tabs.c6 std/text/quote.c6 std/text/nato.c6
  $C6_OUT/stdclean
  ;;

std-color)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcolor examples/stdcolor.c6 std/core/types.c6 std/text/str.c6 std/core/color.c6 std/data/palette.c6 std/text/colorname.c6 std/core/units.c6
  $C6_OUT/stdcolor
  ;;

std-cont)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcontainers examples/stdcontainers.c6 std/core/types.c6 std/text/str.c6 std/container/stack.c6 std/container/queue.c6 std/container/slots.c6 std/container/bag.c6
  $C6_OUT/stdcontainers
  ;;

std-stats)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdstats2 examples/stdstats2.c6 std/core/types.c6 std/text/str.c6 std/core/histogram.c6 std/core/moving.c6 std/core/series.c6 std/core/quantile.c6 -lm
  $C6_OUT/stdstats2
  ;;

std-search)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdsearch examples/stdsearch.c6 std/core/types.c6 std/text/str.c6 std/text/kmp.c6 std/text/bmh.c6 std/text/suffix.c6 std/text/wordplay.c6
  $C6_OUT/stdsearch
  ;;

std-audio2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdlegacy2 examples/stdlegacy2.c6 std/core/types.c6 std/text/str.c6 std/data/aiff.c6 std/data/au.c6 std/data/voc.c6 std/data/ircam.c6
  $C6_OUT/stdlegacy2
  ;;

std-proto3)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdnet2 examples/stdnet2.c6 std/core/types.c6 std/text/str.c6 std/net/irc.c6 std/net/gopher.c6 std/net/whois.c6 std/net/ident.c6
  $C6_OUT/stdnet2
  ;;

std-markup)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdmarkup examples/stdmarkup.c6 std/core/types.c6 std/text/str.c6 std/text/rst.c6 std/text/org.c6 std/text/tex.c6 std/text/pod.c6
  $C6_OUT/stdmarkup
  ;;

std-textan)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdtextan examples/stdtextan.c6 std/core/types.c6 std/text/str.c6 std/text/words.c6 std/text/sentences.c6 std/text/ngram.c6 std/text/readability.c6
  $C6_OUT/stdtextan
  ;;

std-cfg2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdcfg examples/stdcfg.c6 std/core/types.c6 std/text/str.c6 std/data/dotenv.c6 std/data/hosts.c6 std/data/resolvconf.c6 std/data/knownhosts.c6
  $C6_OUT/stdcfg
  ;;

std-match)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdmatch examples/stdmatch.c6 std/core/types.c6 std/text/str.c6 std/text/aho.c6 std/text/rabin.c6 std/container/interval.c6 std/text/align.c6
  $C6_OUT/stdmatch
  ;;

std-async)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdasync examples/stdasync.c6 std/core/types.c6 std/text/str.c6 std/core/future.c6 std/core/timer.c6 std/core/task.c6 std/os/poll.c6 std/os/reactor.c6
  $C6_OUT/stdasync
  ;;

std-async2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdasync2 examples/stdasync2.c6 std/core/types.c6 std/text/str.c6 std/core/future.c6 std/os/poll.c6 std/core/timer.c6 std/core/task.c6 std/os/reactor.c6 std/core/executor.c6 std/os/asio.c6 std/core/retry.c6 std/core/awaitgroup.c6
  $C6_OUT/stdasync2
  ;;

std-math3)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdmath3 examples/stdmath3.c6 std/core/types.c6 std/text/str.c6 std/core/solve.c6 std/core/ode.c6 std/core/optimize.c6 std/core/least.c6 -lm
  $C6_OUT/stdmath3
  ;;

std-linalg)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdlinalg examples/stdlinalg.c6 std/core/types.c6 std/text/str.c6 std/core/lu.c6 std/core/eigen.c6 std/core/tridiag.c6 std/core/sparse.c6 -lm
  $C6_OUT/stdlinalg
  ;;

std-search2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdsearch2 examples/stdsearch2.c6 std/core/types.c6 std/text/str.c6 std/text/inverted.c6 std/text/bm25.c6 std/text/keyword.c6 std/text/stem.c6 -lm
  $C6_OUT/stdsearch2
  ;;

std-graph2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdgraph2 examples/stdgraph2.c6 std/core/types.c6 std/text/str.c6 std/container/mst.c6 std/container/flow.c6 std/container/scc.c6 std/container/match.c6
  $C6_OUT/stdgraph2
  ;;

std-async3)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdasync3 examples/stdasync3.c6 std/core/types.c6 std/text/str.c6 std/core/future.c6 std/os/poll.c6 std/core/timer.c6 std/core/task.c6 std/os/reactor.c6 std/core/executor.c6 std/async/co.c6 std/async/await.c6 std/async/scope.c6 std/async/limiter.c6
  $C6_OUT/stdasync3
  ;;

std-geo2)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdgeo2 examples/stdgeo2.c6 std/core/types.c6 std/text/str.c6 std/core/hull.c6 std/core/polygon.c6 std/core/segment.c6 std/core/closest.c6 -lm
  $C6_OUT/stdgeo2
  ;;

std-async4)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdasync4 examples/stdasync4.c6 std/core/types.c6 std/text/str.c6 std/async/aio.c6 std/async/achan.c6 std/async/apump.c6 std/async/art.c6
  $C6_OUT/stdasync4
  ;;

std-mesh)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdmesh examples/stdmesh.c6 std/core/types.c6 std/text/str.c6 std/data/obj.c6 std/data/stl.c6 std/data/ply.c6 std/data/mtl.c6 -lm
  $C6_OUT/stdmesh
  ;;

std-https)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdhttps examples/stdhttps.c6 std/core/types.c6 std/text/str.c6 std/core/bits.c6 std/encoding/sha256.c6 std/encoding/hkdf.c6 std/encoding/chacha20.c6 std/encoding/poly1305.c6 std/net/tlskdf.c6 std/net/tlsrec.c6 std/net/https.c6 std/net/httpsa.c6 std/os/time.c6 std/async/abench.c6
  $C6_OUT/stdhttps
  ;;

std-async5)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdasync5 examples/stdasync5.c6 std/core/types.c6 std/text/str.c6 std/os/time.c6 std/async/aio.c6 std/async/abench.c6 std/async/abudget.c6 std/async/agroup.c6 std/async/apool.c6 std/async/acancel.c6
  $C6_OUT/stdasync5
  ;;

std-ub)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdub examples/stdub.c6 std/analysis/ub.c6 std/analysis/parse.c6 std/analysis/lex.c6 std/core/mem.c6 std/core/types.c6 std/text/str.c6 std/os/fs.c6 std/os/walk.c6 std/os/path.c6
  $C6_OUT/stdub || test $? -le 1
  ;;

std-sec)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdsec examples/stdsec.c6 std/core/types.c6 std/net/x25519.c6 std/net/hsts.c6 std/net/robots.c6 std/encoding/base45.c6
  $C6_OUT/stdsec
  ;;

std-mw)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdmw examples/stdmw.c6 std/core/types.c6 std/net/ratelimit.c6 std/net/httpcache.c6 std/encoding/quotedprintable.c6 std/text/strdist.c6
  $C6_OUT/stdmw
  ;;

std-proto4)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdproto4 examples/stdproto4.c6 std/core/types.c6 std/net/coap.c6 std/net/dhcp.c6 std/net/ssdp.c6 std/text/translit.c6
  $C6_OUT/stdproto4
  ;;

std-ops)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdops examples/stdops.c6 std/core/types.c6 std/net/netflow.c6 std/net/wol.c6 std/data/vtt.c6 std/data/zone.c6
  $C6_OUT/stdops
  ;;

std-docs)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stddocs examples/stddocs.c6 std/core/types.c6 std/text/plural.c6 std/data/mbox.c6 std/data/kdl.c6 std/data/rtf.c6
  $C6_OUT/stddocs
  ;;

std-mgmt)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdmgmt examples/stdmgmt.c6 std/core/types.c6 std/net/prometheus.c6 std/net/gemini.c6 std/net/snmp.c6 std/net/nntp.c6
  $C6_OUT/stdmgmt
  ;;

std-util)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I . -o $C6_OUT/stdutil examples/stdutil.c6 std/core/types.c6 std/net/cidr.c6 std/core/endian.c6 std/text/collate.c6 std/text/hyphen.c6
  $C6_OUT/stdutil
  ;;

check-tcc)
  test -f $C6_TCC_DIR/tccpp.c || { echo "缺少 $C6_TCC_DIR/ 源码，检出可能不完整"; exit 1; }
  ;;

test)
  $C6_MAKE -C $C6_TCC_DIR/tests/tests2 tests2.all
  ;;

test-plain)
  $C6_MAKE -C $C6_TCC_DIR/tests/tests2 tests2.all
  ;;

selftest)
  ./$C6_SELFTEST
  ;;

errcheck)
  ./$C6_ERRBASE
  ;;

stdtest)
  mkdir -p "$C6_OUT" # std/os/io.c6 的用例要往 $C6_OUT/ 写临时文件，干净树上得先有目录
  fail=0; for f in $(find std -name '*.c6' | sort); do printf '%-26s ' "$f"; case $f in std/container/sort.c6) deps="std/text/str.c6" ;;
  std/container/vec.c6) deps="std/container/sort.c6 std/text/str.c6" ;;
  std/text/strbuf.c6) deps="std/text/fmt.c6" ;;
  std/os/path.c6) deps="std/text/str.c6" ;;
  std/container/set.c6) deps="std/container/map.c6" ;;
  std/container/bitset.c6) deps="std/core/bits.c6" ;;
  std/data/json.c6) deps="std/core/mem.c6 std/text/str.c6 std/text/num.c6 std/core/err.c6" ;;
  std/data/csv.c6) deps="std/text/str.c6" ;;
  std/os/env.c6) deps="std/text/str.c6 std/core/err.c6" ;;
  std/encoding/sha256.c6) deps="std/core/bits.c6" ;;
  std/net/url.c6) deps="std/text/str.c6" ;;
  std/net/mime.c6) deps="std/text/str.c6 std/os/path.c6" ;;
  std/encoding/uuid.c6) deps="std/core/rand.c6 std/os/time.c6" ;;
  std/container/pqueue.c6) deps="std/container/sort.c6 std/text/str.c6" ;;
  std/text/width.c6) deps="std/text/utf8.c6" ;;
  std/encoding/punycode.c6) deps="std/text/utf8.c6" ;;
  std/text/regex.c6) deps="std/text/utf8.c6" ;;
  std/text/fuzzy.c6) deps="std/text/utf8.c6" ;;
  std/text/wrap.c6) deps="std/text/width.c6 std/text/utf8.c6" ;;
  std/text/diff.c6) deps="std/core/mem.c6" ;;
  std/core/complex.c6) deps="std/core/math.c6" ;;
  std/data/toml.c6) deps="std/text/str.c6 std/text/num.c6 std/core/err.c6" ;;
  std/data/xml.c6) deps="std/text/str.c6" ;;
  std/data/query.c6) deps="std/text/str.c6" ;;
  std/text/template.c6) deps="std/text/str.c6" ;;
  std/os/args.c6) deps="std/text/str.c6" ;;
  std/text/table.c6) deps="std/text/width.c6 std/text/utf8.c6" ;;
  std/net/ws.c6) deps="std/encoding/sha1.c6 std/encoding/base64.c6 std/core/err.c6" ;;
  std/container/chan.c6) deps="std/os/sync.c6" ;;
  std/net/resolver.c6) deps="std/net/dns.c6 std/net/udp.c6 std/os/io.c6 std/text/str.c6" ;;
  std/net/httpc.c6) deps="std/net/tcp.c6 std/net/url.c6 std/text/str.c6" ;;
  std/net/httpd.c6) deps="std/net/http.c6 std/net/tcp.c6 std/text/str.c6" ;;
  std/net/router.c6) deps="" ;;
  std/net/assets.c6) deps="std/net/httpd.c6 std/net/http.c6 std/net/tcp.c6 std/net/mime.c6 std/net/url.c6 std/os/fs.c6 std/os/io.c6 std/os/path.c6 std/text/str.c6 std/core/err.c6" ;;
  std/net/session.c6) deps="std/core/rand.c6 std/os/time.c6 std/net/cookie.c6 std/text/str.c6" ;;
  std/encoding/md5.c6) deps="std/core/bits.c6" ;;
  std/encoding/sha512.c6) deps="std/core/bits.c6" ;;
  std/encoding/hkdf.c6) deps="std/encoding/sha256.c6 std/core/bits.c6 std/core/err.c6" ;;
  std/encoding/pbkdf2.c6) deps="std/encoding/sha1.c6 std/encoding/sha256.c6 std/core/bits.c6 std/core/err.c6" ;;
  std/os/walk.c6) deps="std/os/fs.c6 std/os/path.c6 std/text/str.c6 std/core/err.c6" ;;
  std/os/temp.c6) deps="" ;;
  std/os/lock.c6) deps="std/os/time.c6" ;;
  std/os/mmap.c6) deps="" ;;
  std/encoding/aes.c6) deps="" ;;
  std/encoding/chacha20.c6) deps="std/core/bits.c6" ;;
  std/encoding/poly1305.c6) deps="" ;;
  std/encoding/random.c6) deps="" ;;
  std/data/bencode.c6) deps="std/text/str.c6" ;;
  std/data/msgpack.c6) deps="std/text/str.c6" ;;
  std/data/cbor.c6) deps="std/text/str.c6" ;;
  std/data/pem.c6) deps="std/encoding/base64.c6 std/text/str.c6" ;;
  std/core/geometry.c6) deps="" ;;
  std/core/interp.c6) deps="" ;;
  std/core/stats.c6) deps="" ;;
  std/core/matrix.c6) deps="" ;;
  std/encoding/deflate.c6) deps="" ;;
  std/encoding/zlib.c6) deps="std/encoding/deflate.c6" ;;
  std/encoding/gzip.c6) deps="std/encoding/deflate.c6 std/core/crc.c6" ;;
  std/encoding/tar.c6) deps="std/text/str.c6" ;;
  std/data/wav.c6) deps="" ;;
  std/data/png.c6) deps="std/core/crc.c6 std/text/str.c6 std/encoding/zlib.c6 std/encoding/deflate.c6" ;;
  std/data/elf.c6) deps="" ;;
  std/data/zip.c6) deps="std/encoding/deflate.c6 std/core/crc.c6" ;;
  std/data/bmp.c6) deps="" ;;
  std/data/tga.c6) deps="" ;;
  std/data/qoi.c6) deps="" ;;
  std/data/pnm.c6) deps="" ;;
  std/encoding/asn1.c6) deps="std/text/str.c6" ;;
  std/data/x509.c6) deps="std/encoding/asn1.c6 std/text/str.c6" ;;
  std/data/jwt.c6) deps="std/encoding/base64.c6 std/encoding/sha256.c6 std/encoding/sha512.c6 std/core/bits.c6 std/text/str.c6" ;;
  std/encoding/otp.c6) deps="std/encoding/sha1.c6 std/core/bits.c6" ;;
  std/encoding/lzw.c6) deps="" ;;
  std/data/gif.c6) deps="std/encoding/lzw.c6" ;;
  std/data/tiff.c6) deps="std/encoding/zlib.c6 std/encoding/deflate.c6 std/encoding/rle.c6" ;;
  std/data/ico.c6) deps="std/data/png.c6 std/core/crc.c6 std/text/str.c6 std/encoding/zlib.c6 std/encoding/deflate.c6" ;;
  std/data/html.c6) deps="std/text/str.c6" ;;
  std/data/yaml.c6) deps="std/text/str.c6" ;;
  std/data/vcf.c6) deps="std/text/str.c6" ;;
  std/data/ics.c6) deps="std/text/str.c6" ;;
  std/net/sse.c6) deps="std/text/str.c6" ;;
  std/net/socks5.c6) deps="std/net/inet.c6 std/text/str.c6 std/core/err.c6" ;;
  std/net/mqtt.c6) deps="std/text/str.c6" ;;
  std/encoding/snappy.c6) deps="" ;;
  std/data/cpio.c6) deps="std/text/str.c6" ;;
  std/data/pe.c6) deps="std/text/str.c6" ;;
  std/data/macho.c6) deps="std/text/str.c6" ;;
  std/data/wasm.c6) deps="std/text/str.c6" ;;
  std/data/bson.c6) deps="std/text/str.c6" ;;
  std/data/ubjson.c6) deps="std/text/str.c6" ;;
  std/data/plist.c6) deps="std/text/str.c6" ;;
  std/text/markdown.c6) deps="std/text/str.c6" ;;
  std/os/sysinfo.c6) deps="std/text/str.c6" ;;
  std/os/resource.c6) deps="std/text/str.c6" ;;
  std/os/user.c6) deps="std/text/str.c6" ;;
  std/os/pty.c6) deps="std/text/str.c6" ;;
  std/encoding/sha3.c6) deps="std/text/str.c6" ;;
  std/encoding/blake2s.c6) deps="std/text/str.c6" ;;
  std/encoding/crc32c.c6) deps="std/text/str.c6" ;;
  std/encoding/xxhash.c6) deps="std/text/str.c6" ;;
  std/data/pcx.c6) deps="std/text/str.c6" ;;
  std/data/hdr.c6) deps="std/text/str.c6" ;;
  std/data/xbm.c6) deps="std/text/str.c6" ;;
  std/data/xpm.c6) deps="std/text/str.c6" ;;
  std/data/bdf.c6) deps="std/text/str.c6" ;;
  std/data/woff.c6) deps="std/text/str.c6" ;;
  std/data/svg.c6) deps="std/text/str.c6" ;;
  std/data/ttf.c6) deps="std/text/str.c6" ;;
  std/data/mbr.c6) deps="std/text/str.c6" ;;
  std/data/fat.c6) deps="std/text/str.c6" ;;
  std/data/iso.c6) deps="std/text/str.c6" ;;
  std/data/ext2.c6) deps="std/text/str.c6" ;;
  std/encoding/base91.c6) deps="std/text/str.c6" ;;
  std/encoding/uu.c6) deps="std/text/str.c6" ;;
  std/encoding/yenc.c6) deps="std/text/str.c6" ;;
  std/encoding/ebcdic.c6) deps="std/text/str.c6" ;;
  std/data/mp3.c6) deps="std/text/str.c6" ;;
  std/data/ogg.c6) deps="std/text/str.c6" ;;
  std/data/flac.c6) deps="std/text/str.c6" ;;
  std/data/midi.c6) deps="std/text/str.c6" ;;
  std/net/smtp.c6) deps="std/text/str.c6" ;;
  std/net/ftp.c6) deps="std/text/str.c6" ;;
  std/net/redis.c6) deps="std/text/str.c6" ;;
  std/net/telnet.c6) deps="std/text/str.c6" ;;
  std/container/bloom.c6) deps="std/text/str.c6" ;;
  std/text/slug.c6) deps="std/text/str.c6" ;;
  std/text/casing.c6) deps="std/text/str.c6" ;;
  std/text/roman.c6) deps="std/text/str.c6" ;;
  std/text/phonetic.c6) deps="std/text/str.c6" ;;
  std/os/iso8601.c6) deps="std/text/str.c6 std/os/calendar.c6" ;;
  std/os/cron.c6) deps="std/text/str.c6 std/os/calendar.c6" ;;
  std/net/ntp.c6) deps="std/text/str.c6" ;;
  std/net/syslog.c6) deps="std/text/str.c6" ;;
  std/net/stun.c6) deps="std/text/str.c6" ;;
  std/net/tftp.c6) deps="std/text/str.c6" ;;
  std/os/log.c6) deps="std/text/str.c6" ;;
  std/data/sqlite.c6) deps="std/text/str.c6" ;;
  std/net/postgres.c6) deps="std/text/str.c6" ;;
  std/net/mysql.c6) deps="std/text/str.c6" ;;
  std/encoding/fnv.c6) deps="std/text/str.c6" ;;
  std/encoding/adler32.c6) deps="std/text/str.c6" ;;
  std/encoding/murmur3.c6) deps="std/text/str.c6" ;;
  std/encoding/siphash.c6) deps="std/text/str.c6" ;;
  std/container/counter.c6) deps="std/text/str.c6" ;;
  std/encoding/bwt.c6) deps="std/text/str.c6" ;;
  std/encoding/mtf.c6) deps="std/text/str.c6" ;;
  std/encoding/delta.c6) deps="std/text/str.c6" ;;
  std/encoding/huffman.c6) deps="std/text/str.c6" ;;
  std/data/srt.c6) deps="std/text/str.c6" ;;
  std/data/ass.c6) deps="std/text/str.c6" ;;
  std/data/lrc.c6) deps="std/text/str.c6" ;;
  std/data/m3u.c6) deps="std/text/str.c6" ;;
  std/data/netrc.c6) deps="std/text/str.c6" ;;
  std/data/sshconfig.c6) deps="std/text/str.c6" ;;
  std/data/keys.c6) deps="std/text/str.c6" ;;
  std/data/crontab.c6) deps="std/text/str.c6" ;;
  std/data/geohash.c6) deps="std/text/str.c6 std/core/geo.c6" ;;
  std/data/maidenhead.c6) deps="std/text/str.c6 std/core/geo.c6" ;;
  std/data/dms.c6) deps="std/text/str.c6" ;;
  std/data/ar.c6) deps="std/text/str.c6" ;;
  std/data/coff.c6) deps="std/text/str.c6" ;;
  std/data/class.c6) deps="std/text/str.c6" ;;
  std/data/rpm.c6) deps="std/text/str.c6" ;;
  std/encoding/luhn.c6) deps="std/text/str.c6" ;;
  std/encoding/crc16.c6) deps="std/text/str.c6" ;;
  std/encoding/hamming.c6) deps="std/text/str.c6" ;;
  std/encoding/classic.c6) deps="std/text/str.c6" ;;
  std/os/mounts.c6) deps="std/text/str.c6" ;;
  std/os/fstab.c6) deps="std/text/str.c6" ;;
  std/os/osrelease.c6) deps="std/text/str.c6" ;;
  std/os/systemd.c6) deps="std/text/str.c6" ;;
  std/core/binio.c6) deps="std/text/str.c6" ;;
  std/core/bitio.c6) deps="std/text/str.c6" ;;
  std/core/half.c6) deps="std/text/str.c6" ;;
  std/core/align.c6) deps="std/text/str.c6" ;;
  std/text/sanitize.c6) deps="std/text/str.c6" ;;
  std/text/tabs.c6) deps="std/text/str.c6" ;;
  std/text/quote.c6) deps="std/text/str.c6" ;;
  std/text/nato.c6) deps="std/text/str.c6" ;;
  std/data/palette.c6) deps="std/text/str.c6 std/core/color.c6" ;;
  std/text/colorname.c6) deps="std/text/str.c6 std/core/color.c6" ;;
  std/core/color.c6) deps="std/text/str.c6" ;;
  std/core/units.c6) deps="std/text/str.c6" ;;
  std/container/stack.c6) deps="std/text/str.c6" ;;
  std/container/queue.c6) deps="std/text/str.c6" ;;
  std/container/slots.c6) deps="std/text/str.c6" ;;
  std/container/bag.c6) deps="std/text/str.c6" ;;
  std/core/histogram.c6) deps="std/text/str.c6" ;;
  std/core/moving.c6) deps="std/text/str.c6" ;;
  std/core/series.c6) deps="std/text/str.c6" ;;
  std/core/quantile.c6) deps="std/text/str.c6" ;;
  std/text/kmp.c6) deps="std/text/str.c6" ;;
  std/text/bmh.c6) deps="std/text/str.c6" ;;
  std/text/suffix.c6) deps="std/text/str.c6" ;;
  std/text/wordplay.c6) deps="std/text/str.c6" ;;
  std/data/aiff.c6) deps="std/text/str.c6" ;;
  std/data/au.c6) deps="std/text/str.c6" ;;
  std/data/voc.c6) deps="std/text/str.c6" ;;
  std/data/ircam.c6) deps="std/text/str.c6" ;;
  std/net/irc.c6) deps="std/text/str.c6" ;;
  std/net/gopher.c6) deps="std/text/str.c6" ;;
  std/net/whois.c6) deps="std/text/str.c6" ;;
  std/net/ident.c6) deps="std/text/str.c6" ;;
  std/text/rst.c6) deps="std/text/str.c6" ;;
  std/text/org.c6) deps="std/text/str.c6" ;;
  std/text/tex.c6) deps="std/text/str.c6" ;;
  std/text/pod.c6) deps="std/text/str.c6" ;;
  std/text/words.c6) deps="std/text/str.c6" ;;
  std/text/sentences.c6) deps="std/text/str.c6" ;;
  std/text/ngram.c6) deps="std/text/str.c6" ;;
  std/text/readability.c6) deps="std/text/str.c6 std/text/words.c6 std/text/sentences.c6" ;;
  std/data/dotenv.c6) deps="std/text/str.c6" ;;
  std/data/hosts.c6) deps="std/text/str.c6" ;;
  std/data/resolvconf.c6) deps="std/text/str.c6" ;;
  std/data/knownhosts.c6) deps="std/text/str.c6" ;;
  std/text/aho.c6) deps="std/text/str.c6" ;;
  std/text/rabin.c6) deps="std/text/str.c6" ;;
  std/container/interval.c6) deps="std/text/str.c6" ;;
  std/text/align.c6) deps="std/text/str.c6" ;;
  std/core/future.c6) deps="std/text/str.c6" ;;
  std/core/timer.c6) deps="std/text/str.c6" ;;
  std/core/task.c6) deps="std/text/str.c6" ;;
  std/os/reactor.c6) deps="std/text/str.c6 std/os/poll.c6 std/core/timer.c6" ;;
  std/core/executor.c6) deps="std/text/str.c6 std/os/poll.c6 std/core/timer.c6 std/core/task.c6 std/os/reactor.c6" ;;
  std/os/asio.c6) deps="std/text/str.c6 std/os/poll.c6 std/core/timer.c6 std/os/reactor.c6" ;;
  std/core/retry.c6) deps="std/text/str.c6" ;;
  std/core/awaitgroup.c6) deps="std/text/str.c6 std/core/future.c6" ;;
  std/core/solve.c6) deps="std/text/str.c6 -lm" ;;
  std/core/ode.c6) deps="std/text/str.c6 -lm" ;;
  std/core/optimize.c6) deps="std/text/str.c6 -lm" ;;
  std/core/least.c6) deps="std/text/str.c6 -lm" ;;
  std/core/lu.c6) deps="std/text/str.c6 -lm" ;;
  std/core/eigen.c6) deps="std/text/str.c6 -lm" ;;
  std/core/tridiag.c6) deps="std/text/str.c6 -lm" ;;
  std/core/sparse.c6) deps="std/text/str.c6 -lm" ;;
  std/text/inverted.c6) deps="std/text/str.c6" ;;
  std/text/stem.c6) deps="std/text/str.c6" ;;
  std/text/keyword.c6) deps="std/text/str.c6" ;;
  std/text/bm25.c6) deps="std/text/str.c6 -lm" ;;
  std/container/mst.c6) deps="std/text/str.c6" ;;
  std/container/flow.c6) deps="std/text/str.c6" ;;
  std/container/scc.c6) deps="std/text/str.c6" ;;
  std/container/match.c6) deps="std/text/str.c6" ;;
  std/async/co.c6) deps="std/text/str.c6 std/core/future.c6 std/os/poll.c6 std/core/timer.c6 std/core/task.c6 std/os/reactor.c6 std/core/executor.c6" ;;
  std/async/await.c6) deps="std/text/str.c6 std/core/future.c6 std/os/poll.c6 std/core/timer.c6 std/core/task.c6 std/os/reactor.c6 std/core/executor.c6 std/async/co.c6" ;;
  std/async/scope.c6) deps="std/text/str.c6 std/core/future.c6 std/os/poll.c6 std/core/timer.c6 std/core/task.c6 std/os/reactor.c6 std/core/executor.c6 std/async/co.c6" ;;
  std/async/limiter.c6) deps="std/text/str.c6" ;;
  std/core/hull.c6) deps="std/text/str.c6 -lm" ;;
  std/core/polygon.c6) deps="std/text/str.c6 -lm" ;;
  std/core/segment.c6) deps="std/text/str.c6 -lm" ;;
  std/core/closest.c6) deps="std/text/str.c6 -lm" ;;
  std/async/aio.c6) deps="std/text/str.c6" ;;
  std/async/achan.c6) deps="std/text/str.c6 std/async/aio.c6" ;;
  std/async/art.c6) deps="std/text/str.c6 std/async/aio.c6" ;;
  std/async/apump.c6) deps="std/text/str.c6 std/async/aio.c6 std/async/achan.c6" ;;
  std/data/obj.c6) deps="std/text/str.c6" ;;
  std/data/stl.c6) deps="std/text/str.c6 -lm" ;;
  std/data/ply.c6) deps="std/text/str.c6" ;;
  std/data/mtl.c6) deps="std/text/str.c6" ;;
  std/net/tlskdf.c6) deps="std/text/str.c6 std/core/bits.c6 std/encoding/sha256.c6 std/encoding/hkdf.c6" ;;
  std/net/tlsrec.c6) deps="std/text/str.c6 std/core/bits.c6 std/encoding/chacha20.c6 std/encoding/poly1305.c6" ;;
  std/net/https.c6) deps="std/text/str.c6 std/core/bits.c6 std/encoding/chacha20.c6 std/encoding/poly1305.c6 std/net/tlsrec.c6" ;;
  std/net/httpsa.c6) deps="std/text/str.c6 std/core/bits.c6 std/encoding/chacha20.c6 std/encoding/poly1305.c6 std/net/tlsrec.c6" ;;
  std/async/abench.c6) deps="std/text/str.c6 std/os/time.c6" ;;
  std/async/abudget.c6) deps="std/text/str.c6 std/os/time.c6" ;;
  std/async/agroup.c6) deps="std/text/str.c6 std/async/aio.c6" ;;
  std/async/apool.c6) deps="std/text/str.c6 std/async/aio.c6" ;;
  std/async/acancel.c6) deps="std/text/str.c6" ;;
  std/analysis/lex.c6) deps="std/text/str.c6" ;;
  std/analysis/parse.c6) deps="std/analysis/lex.c6 std/core/mem.c6 std/text/str.c6" ;;
  std/analysis/ub.c6) deps="std/analysis/parse.c6 std/analysis/lex.c6 std/core/mem.c6 std/text/str.c6" ;;
  *) deps="" ;;
  esac; out=$("$C6_TCC_DIR/c6" -B $C6_TCC_DIR -I . -test $f $deps 2>&1) || fail=1; echo "$out" | tail -1; done; test $fail -eq 0
  ;;

ubcheck)
  "$0" std-ub
  rc=0
  $C6_OUT/stdub -W std > "$C6_TMP/ub.out" 2> "$C6_TMP/ub.err" || rc=$?
  cat "$C6_TMP/ub.out"; cat "$C6_TMP/ub.err"
  if [ "$rc" -ne 0 ]; then echo "ubcheck: std/ 没通过安全检查（退出码 $rc）"; exit 1; fi
  echo "ubcheck: std/ 全部通过（十五类可判定 UB 0 条、无「没看懂」构造）"
  ;;

safegate)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B $C6_TCC_DIR -I . -b -o $C6_OUT/ubsmoke tests/ub/ubsmoke.c6
  if $C6_OUT/ubsmoke >/dev/null 2>&1; then echo "safegate: 运行时 -b 没抓住 tests/ub/ubsmoke.c6 的越界，第二层失效"; exit 1; fi
  echo "safegate: 运行时 -b 抓住了静态判不出的越界（变量下标）"
  echo "safegate: 静态 + 运行时两层都通过"
  ;;

fstest)
  ./tcc/c6 -B tcc -static -nostdlib -o $C6_OUT/freestanding_fstr examples/freestanding_fstr.c6 tcc/libtcc1.a
  $C6_OUT/freestanding_fstr
  ;;

fstest-go)
  ./tcc/c6 -B tcc -c -o $C6_OUT/freestanding_pthread.o examples/freestanding_pthread.c
  ./tcc/c6 -B tcc -static -nostdlib -o $C6_OUT/freestanding_go examples/freestanding_go.c6 $C6_OUT/freestanding_pthread.o tcc/libtcc1.a
  $C6_OUT/freestanding_go
  ;;

fstest-noos)
  mkdir -p "$C6_OUT"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -static -nostdlib -o $C6_OUT/freestanding_noos examples/freestanding_noos.c6 "$C6_TCC_DIR/libtcc1.a"
  st=0
  $C6_OUT/freestanding_noos 2>/dev/null || st=$?
  if [ "$st" -eq 132 ]; then
  echo "fstest-noos: ok（全部断言通过，最后 ud2 陷阱）"
  else
  echo "fstest-noos: FAILED（status $st，失败位掩码见程序）"; exit 1
  fi
  ;;

fstest-qemu)
  "$0" fstest; "$0" fstest-go; "$0" fstest-noos
  command -v qemu-x86_64 >/dev/null || { echo "fstest-qemu: 没有 qemu-x86_64，跳过"; exit 0; }
  qemu-x86_64 $C6_OUT/freestanding_fstr
  qemu-x86_64 $C6_OUT/freestanding_go
  st=0
  qemu-x86_64 $C6_OUT/freestanding_noos >/dev/null 2>&1 || st=$?
  if [ "$st" -eq 132 ]; then
  echo "fstest-qemu: noos ok（ud2 陷阱）"
  else
  echo "fstest-qemu: noos FAILED（status $st）"; exit 1
  fi
  ;;

fstest-bare)
  command -v qemu-system-x86_64 >/dev/null || { echo "fstest-bare: 没有 qemu-system-x86_64，跳过"; exit 0; }
  command -v as >/dev/null || { echo "fstest-bare: 没有 binutils，跳过"; exit 0; }
  BARE_BUILD=$C6_BARE/boot ./examples/boot/build.sh
  ;;

fstest-bare-tests)
  command -v qemu-system-x86_64 >/dev/null || { echo "fstest-bare-tests: 没有 qemu-system-x86_64，跳过"; exit 0; }
  command -v as >/dev/null || { echo "fstest-bare-tests: 没有 binutils，跳过"; exit 0; }
  BARE_BUILD=$C6_BARE/tests ./examples/boot/runtests.sh
  ;;

fstest-bare-go)
  command -v qemu-system-x86_64 >/dev/null || { echo "fstest-bare-go: 没有 qemu-system-x86_64，跳过"; exit 0; }
  command -v as >/dev/null || { echo "fstest-bare-go: 没有 binutils，跳过"; exit 0; }
  BARE_BUILD=$C6_BARE/go ./examples/boot/build.sh examples/boot/go_bare.c6 examples/boot/sched.plain.c examples/boot/shim.plain.c examples/boot/io.c
  ;;

fstest-bare-handle)
  command -v qemu-system-x86_64 >/dev/null || { echo "fstest-bare-handle: 没有 qemu-system-x86_64，跳过"; exit 0; }
  command -v as >/dev/null || { echo "fstest-bare-handle: 没有 binutils，跳过"; exit 0; }
  BARE_BUILD=$C6_BARE/handle SMP=1 ./examples/boot/build.sh examples/boot/handle_bare.c6 examples/boot/sched.plain.c examples/boot/shim.plain.c examples/boot/io.c
  ;;

fstest-bare-smp)
  command -v qemu-system-x86_64 >/dev/null || { echo "fstest-bare-smp: 没有 qemu-system-x86_64，跳过"; exit 0; }
  command -v as >/dev/null || { echo "fstest-bare-smp: 没有 binutils，跳过"; exit 0; }
  BARE_BUILD=$C6_BARE/smp2 SMP=2 EXTRA_CFLAGS=-DC6_SMP_CPUS=2 ./examples/boot/build.sh examples/boot/smp_bare.c6 examples/boot/sched.plain.c examples/boot/shim.plain.c examples/boot/io.c
  BARE_BUILD=$C6_BARE/smp1 SMP=1 EXTRA_CFLAGS=-DC6_SMP_CPUS=1 ./examples/boot/build.sh examples/boot/smp_bare.c6 examples/boot/sched.plain.c examples/boot/shim.plain.c examples/boot/io.c
  ;;

c99test)
  BACKEND=c99 ./$C6_SELFTEST
  ;;

c99tcc-test)
  BACKEND=c99 C99CC=$PWD/$C6_TCC_DIR/c6 C99FLAGS="-B $PWD/$C6_TCC_DIR -std=c99 -w" ./$C6_SELFTEST
  ;;

bench-async)
  ./bench/async/run.sh
  ;;

c6selfcheck)
  mkdir -p "$C6_SELFHOST/c6"
  cp "$C6_TCC_DIR/tcc.c" "$C6_SELFHOST/c6/selfcheck.c6"
  out=$("$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I "$C6_TCC_DIR" -DC6_EXT -c "$C6_SELFHOST/c6/selfcheck.c6" -o "$C6_SELFHOST/c6/selfcheck.o" 2>&1)
  if [ -n "$out" ]; then printf "%s\n" "$out" >&2; echo "c6selfcheck: 用 tcc 编译自身时不应有任何诊断输出" >&2; exit 1; fi
  out=$("$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -I "$C6_TCC_DIR" -DC6_EXT -o "$C6_SELFHOST/c6/selfcheck.c" "$C6_SELFHOST/c6/selfcheck.c6" 2>&1)
  if [ -n "$out" ]; then printf "%s\n" "$out" >&2; echo "c6selfcheck: 用 tcc 生成 C 时不应有任何诊断输出" >&2; exit 1; fi
  "$C6_HOSTCC" -std=c99 -c -o "$C6_SELFHOST/c6/selfcheck-c99.o" "$C6_SELFHOST/c6/selfcheck.c"
  "$C6_TCC_DIR/c6" -B "$C6_TCC_DIR" -o "$C6_SELFHOST/c6/selfcheck" "$C6_SELFHOST/c6/selfcheck.o" -lm -ldl -lpthread
  "$C6_SELFHOST/c6/selfcheck" -v
  ;;

selfhost)
  mkdir -p $C6_SELFHOST/c6
  echo "c6 自举 1/4：$C6_TCC_DIR/c6 编译自己的源码"
  $C6_TCC_DIR/c6 -B $C6_TCC_DIR -DONE_SOURCE=1 -DC6_EXT -c $C6_TCC_DIR/tcc.c -o $C6_SELFHOST/c6/stage1.o
  $C6_TCC_DIR/c6 -B $C6_TCC_DIR -o $C6_SELFHOST/c6/tcc-stage1 $C6_SELFHOST/c6/stage1.o -lm -ldl -lpthread
  echo "c6 自举 2/4：stage1 重新编译同一份源码"
  $C6_SELFHOST/c6/tcc-stage1 -B $C6_TCC_DIR -DONE_SOURCE=1 -DC6_EXT -c $C6_TCC_DIR/tcc.c -o $C6_SELFHOST/c6/stage2.o
  echo "c6 自举 3/4：比较两次的目标文件"
  cmp $C6_SELFHOST/c6/stage1.o $C6_SELFHOST/c6/stage2.o && echo " 两次编译逐字节相同"
  $C6_SELFHOST/c6/tcc-stage1 -B $C6_TCC_DIR -o $C6_SELFHOST/c6/tcc-stage2 $C6_SELFHOST/c6/stage2.o -lm -ldl -lpthread
  echo "c6 自举 4/4：stage2 编一个程序并运行"
  printf '%s\n' '#include <stdio.h>' 'int main(void)' '{' ' printf("hello from the c6 self-hosted tcc\n");' ' return 0;' '}' > $C6_SELFHOST/c6/hello.c
  $C6_SELFHOST/c6/tcc-stage2 -B $C6_TCC_DIR $C6_SELFHOST/c6/hello.c -o $C6_SELFHOST/c6/hello
  $C6_SELFHOST/c6/hello
  echo "c6 自举完成：$C6_SELFHOST/c6/tcc-stage2"
  ;;

c99selfhost)
  mkdir -p $C6_SELFHOST/c99
  echo "c99 自举 1/4：tcc 的 C99 后端把 tcc.c 翻成 C"
  $C6_TCC_DIR/c6 -B $C6_TCC_DIR -DONE_SOURCE=1 -DC6_EXT -c99 -o $C6_SELFHOST/c99/tcc-c99.c $C6_TCC_DIR/tcc.c
  echo "c99 自举 2/4：宿主 $C6_HOSTCC 编译生成的 C，得到 stage2 编译器"
  $C6_HOSTCC -std=c99 -O0 -w -o $C6_SELFHOST/c99/tcc-stage2 $C6_SELFHOST/c99/tcc-c99.c -lm -ldl -lpthread
  echo "c99 自举 3/4：stage2 编译 tcc.c 得到 stage3"
  $C6_SELFHOST/c99/tcc-stage2 -B $C6_TCC_DIR -DONE_SOURCE=1 -DC6_EXT -c $C6_TCC_DIR/tcc.c -o $C6_SELFHOST/c99/stage3.o
  $C6_SELFHOST/c99/tcc-stage2 -B $C6_TCC_DIR -o $C6_SELFHOST/c99/tcc-stage3 $C6_SELFHOST/c99/stage3.o -lm -ldl -lpthread
  echo "c99 自举 4/4：stage3 再编一次并比较目标文件"
  $C6_SELFHOST/c99/tcc-stage3 -B $C6_TCC_DIR -DONE_SOURCE=1 -DC6_EXT -c $C6_TCC_DIR/tcc.c -o $C6_SELFHOST/c99/stage4.o
  cmp $C6_SELFHOST/c99/stage3.o $C6_SELFHOST/c99/stage4.o && echo " 两次编译逐字节相同"
  printf '%s\n' '#include <stdio.h>' 'int main(void)' '{' ' printf("hello from the c99 self-hosted tcc\n");' ' return 0;' '}' > $C6_SELFHOST/c99/hello.c
  $C6_SELFHOST/c99/tcc-stage3 -B $C6_TCC_DIR $C6_SELFHOST/c99/hello.c -o $C6_SELFHOST/c99/hello
  $C6_SELFHOST/c99/hello
  echo "c99 自举完成：$C6_SELFHOST/c99/tcc-stage3"
  ;;

c99selfhost-tcc)
  mkdir -p $C6_SELFHOST/c99tcc
  echo "c99+tcc 自举 1/4：tcc 的 C99 后端把 tcc.c 翻成 C"
  $C6_TCC_DIR/c6 -B $C6_TCC_DIR -DONE_SOURCE=1 -DC6_EXT -c99 -o $C6_SELFHOST/c99tcc/tcc-c99.c $C6_TCC_DIR/tcc.c
  echo "c99+tcc 自举 2/4：同一个 tcc 编译生成的 C，得到 stage2 编译器"
  $C6_TCC_DIR/c6 -B $C6_TCC_DIR -o $C6_SELFHOST/c99tcc/tcc-stage2 $C6_SELFHOST/c99tcc/tcc-c99.c -lm -ldl -lpthread
  echo "c99+tcc 自举 3/4：stage2 编一遍 tcc.c，tcc 自己也编一遍"
  $C6_SELFHOST/c99tcc/tcc-stage2 -B $C6_TCC_DIR -DONE_SOURCE=1 -DC6_EXT -c $C6_TCC_DIR/tcc.c -o $C6_SELFHOST/c99tcc/stage3.o
  $C6_TCC_DIR/c6 -B $C6_TCC_DIR -DONE_SOURCE=1 -DC6_EXT -c $C6_TCC_DIR/tcc.c -o $C6_SELFHOST/c99tcc/native.o
  echo "c99+tcc 自举 4/4：比较两个目标文件"
  cmp $C6_SELFHOST/c99tcc/native.o $C6_SELFHOST/c99tcc/stage3.o && echo " 逐字节相同"
  printf '%s\n' '#include <stdio.h>' 'int main(void)' '{' ' printf("hello from the tcc-hosted c99 self-hosted tcc\n");' ' return 0;' '}' > $C6_SELFHOST/c99tcc/hello.c
  $C6_SELFHOST/c99tcc/tcc-stage2 -B $C6_TCC_DIR $C6_SELFHOST/c99tcc/hello.c -o $C6_SELFHOST/c99tcc/hello
  $C6_SELFHOST/c99tcc/hello
  echo "c99+tcc 自举完成：$C6_SELFHOST/c99tcc/tcc-stage2"
  ;;


*)
  printf 'regression: 未知用例 %s\n' "${1:-}" >&2
  exit 2
  ;;
esac
