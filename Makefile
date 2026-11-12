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
        stdtest errcheck std-demo std-tour std-kit std-box

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

# std/：标准库。每个文件一个 module（模块名就是路径：std/vec.c6 -> std.vec），
# 每个文件自带 test 块，逐个跑。
#
# 模块之间可以互相 import（`std/vec.c6` 的 sort 就用 `std/sort.c6` 的内核），
# 而 import 只带来**声明**：被依赖模块的 .c6 正文也要放上命令行。所以下面
# 先跑被依赖的 `std/sort.c6`，其余模块按文件逐个跑，需要额外依赖的在 case
# 里列出。没列出来的模块自成一体，单独跑。

stdtest: build
	@mkdir -p build   # std/io.c6 的用例要往 build/ 写临时文件，干净树上得先有目录
	@fail=0; for f in std/str.c6 std/sort.c6 \
	         $$(ls std/*.c6 | grep -vE '/(str|sort)\.c6$$'); do \
	    printf '%-16s ' "$$f"; \
	    case $$f in \
	    std/sort.c6) deps="std/str.c6" ;; \
	    std/vec.c6)  deps="std/sort.c6 std/str.c6" ;; \
	    std/strbuf.c6) deps="std/fmt.c6" ;; \
	    std/path.c6) deps="std/str.c6" ;; \
	    std/set.c6)  deps="std/map.c6" ;; \
	    std/json.c6) deps="std/mem.c6 std/str.c6 std/num.c6 std/err.c6" ;; \
	    std/csv.c6)  deps="std/str.c6" ;; \
	    std/env.c6)  deps="std/str.c6 std/err.c6" ;; \
	    std/sha256.c6) deps="std/bits.c6" ;; \
	    *)           deps="" ;; \
	    esac; \
	    out=$$(./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -test $$f $$deps 2>&1) || fail=1; \
	    echo "$$out" | tail -1; \
	done; \
	test $$fail -eq 0

# std-demo 的源文件清单：示例两个文件 + 用到的模块正文。清单必须覆盖被
# import 模块的正文（import 只带来声明）——`std/vec.c6` 的排序内核在
# `std/sort.c6` 里，sort 自己又依赖 std/str.c6、std/err.c6、std/types.c6。
# 改动 std/ 里的 import 时，这里和上面 stdtest 的 case 要一起更新。
STDDEMO_SRCS := examples/wordcount.c6 examples/wordcount/text.c6 \
                std/types.c6 std/err.c6 std/str.c6 std/sort.c6 \
                std/vec.c6 std/span.c6 std/fmt.c6 std/range.c6

# 标准库的拼装示例：模块正文要单独编进程序（import 只带来声明），
# 模块搜索路径靠 -I 给出（import wordcount.text 找 examples/wordcount/text.c6）。
std-demo: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I examples -I . -o build/wordcount $(STDDEMO_SRCS)
	./build/wordcount

# 标准库一日游：把新加的模块（strbuf/utf8/bits/hash/path/deque）串成一个
# 程序，顺带当它们的集成用例。模块正文照样要一个个列出来。
STDTOUR_SRCS := examples/stdtour.c6 std/types.c6 std/err.c6 std/str.c6 \
                std/span.c6 std/fmt.c6 std/strbuf.c6 std/utf8.c6 \
                std/bits.c6 std/hash.c6 std/path.c6 std/deque.c6

std-tour: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdtour $(STDTOUR_SRCS)
	./build/stdtour

# 标准库第三批的一日游：ascii / num / codec / rand / fs / set。
# set 是 map 的薄包装，所以 map 的正文也要在命令行上。
STDKIT_SRCS := examples/stdkit.c6 std/types.c6 std/err.c6 std/map.c6 \
               std/set.c6 std/ascii.c6 std/num.c6 std/codec.c6 \
               std/rand.c6 std/fs.c6

std-kit: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdkit $(STDKIT_SRCS)
	./build/stdkit

# 标准库第四批：json / sha256 / csv / env。
# json 的 DOM 住在 std.mem 的 Arena 上，字符串视图来自 std.str，数字格式化
# 用 std.num；csv 也借 std.str 的 Str；env 依赖 std.str 与 std.err。
STDBOX_SRCS := examples/stdbox.c6 std/types.c6 std/err.c6 std/str.c6 \
               std/mem.c6 std/num.c6 std/json.c6 std/bits.c6 \
               std/sha256.c6 std/csv.c6 std/env.c6

std-box: build
	@mkdir -p build
	./$(TCC_DIR)/c6 -B $(TCC_DIR) -I . -o build/stdbox $(STDBOX_SRCS)
	./build/stdbox

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

test-all: selftest test c6selfcheck stdtest std-demo std-tour std-kit std-box fstest fstest-go \
	  fstest-noos fstest-qemu \
	  fstest-bare fstest-bare-tests fstest-bare-go fstest-bare-handle \
	  fstest-bare-smp

# 完整验收：test-all 之外还有 C99 后端的两套用例（96 组各跑一遍）和三条
# 自举路线（c6 / c99+gcc / c99+tcc，每条都比对目标文件是否逐字节相同）。
# 这些目标过去只能手动跑，发布前用这一条把它们全部钉住。
#
# 标准库的五个目标在这里显式列出（它们已经在 test-all 里，列出来是为了让
# 「发布前这一条」的覆盖面一眼可见）：stdtest 是 std/ 下 29 个模块逐个跑
# test 块，std-demo/tour/kit/box 是四批模块拼出来的集成示例。以后 test-all
# 增删标准库目标，也不会悄悄改变 check-all 的语义。
check-all: test-all errcheck c99test c99tcc-test selfhost c99selfhost c99selfhost-tcc \
           stdtest std-demo std-tour std-kit std-box

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
#                    + 标准库的 stdtest/std-demo/std-tour/std-kit/std-box）
#
# 标准库
#   stdtest          std/ 下每个模块的 test 块
#   std-demo         编译并运行 examples/wordcount.c6
#   std-tour         编译并运行 examples/stdtour.c6
#   std-kit          编译并运行 examples/stdkit.c6（第三批）
#   std-box          编译并运行 examples/stdbox.c6（第四批）
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
