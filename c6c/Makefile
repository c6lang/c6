# c6c —— 用 c6 写的 c6 编译器（独立子项目）
#
# 这个 Makefile 不引用 tcc/ 的源码：tcc 只作为 bootstrap 第一步与 oracle，
# 路径从 C6C 传进来。顶层 Makefile 有同名转发目标（make c6c-bench 等）。
#
#   make bench        用 tcc/c6 跑基线（P0 起的现状；BOOTSTRAP=host 时不可用）
#   make bench-c6c    用 c6c 跑并按 thresholds.json 断言（P3b 之后才有意义）
#   make lint         隔离检查：不 include tcc 头、不链接 libtcc、不复制 tcc 源码
#   make fast         快速门禁（≈40 s，含外部 c-testsuite 与上游 tests1·tests2）
#   make check-all    最终验收（P8-1 之前各目标会如实报“未实现”）
#
# 引导编译器（stage0）可换，`BOOTSTRAP` 选路：
#
#   BOOTSTRAP=tcc（默认）  ../tcc/c6 编 stage1，`-B ../tcc` 带 tcc 的 crt /
#                          libtcc1.a。要求先构建 tcc。
#   BOOTSTRAP=host        宿主 C 编译器直接编 stage1：c6c 的源码是 C99 兼容的
#                          单 TU（src/c6c.c6 把各模块 #include 进来），
#                          `cc -x c -O2 -o build/c6c src/c6c.c6` 就够了。
#                          这条路**不需要任何 tcc 二进制**，CI 的 no-tcc 作业
#                          与 `cmake -DC6_ENABLE_TCC=OFF` 都走它。
#
# 两条路的产物是同一份源码编出来的同一套语义：fixpoint（c6c2 == c6c3）与
# 其余验收都与 stage0 无关。

PYTHON ?= python3
BOOTSTRAP ?= tcc             # tcc | host（见上面的说明）
# CMAKE 只在需要生成 tcc 的配置头时用到（见 build-tcc）；不要写成行尾注释，
# GNU make 会把注释前的空格留在变量值里，`"$(CMAKE)"` 就找不到命令了。
CMAKE  ?= cmake
C6C    ?= ../tcc/c6          # BOOTSTRAP=tcc 的引导编译器
BDIR   ?= ../tcc             # tcc 的 -B：crt / libtcc1.a
HOSTCC ?= cc                 # BOOTSTRAP=host 的引导编译器
HOSTCFLAGS ?= -O2            # 引导编译器的编译参数（不是 c6c 编用户代码的参数）
BUILD  ?= build
C6FLAGS ?= -g
ROOT   ?= ..
RUNS   ?= 5

SRCS := $(wildcard src/*.c6)

.PHONY: all g2c-check tests1-check scoreboard build scan unit irtest hello ctest runflag testflag tcc-tests bench bench-c6c lint selfhost \
        selftest stdtest test c99test c99suite llvmtest build-tcc fast check-all check-all-perf help \
        objcheck ldcheck libc6c embedcheck

all: build

# stage1：单 TU 引导（见 src/c6c.c6 顶部注释）。BOOTSTRAP 决定用谁编这一遍。
build: $(BUILD)/c6c
	@echo "c6c: $(BUILD)/c6c 已就绪（stage0 = $(BOOTSTRAP)）"

ifeq ($(BOOTSTRAP),host)
$(BUILD)/c6c: $(SRCS)
	@mkdir -p $(BUILD)
	$(HOSTCC) -x c $(HOSTCFLAGS) -o $@ src/c6c.c6
else ifeq ($(BOOTSTRAP),tcc)
$(BUILD)/c6c: $(SRCS)
	@mkdir -p $(BUILD)
	$(C6C) -B $(BDIR) $(C6FLAGS) -o $@ src/c6c.c6
else
$(BUILD)/c6c: $(SRCS)
	@echo "c6c: BOOTSTRAP=$(BOOTSTRAP) 不认识（只有 tcc / host）" >&2
	@exit 1
endif

# 用 c6c 自己的词法扫一遍 tcc 的全部源码与 std/ 的 153 个模块（P1-4）
# il-gen.c 自带 `#error this code has bit-rotted since 2003`，是上游故意留的。
scan: $(BUILD)/c6c
	@ok=0; bad=0; \
	for f in $$(ls $(ROOT)/tcc/*.c | grep -v il-gen.c) \
	         $$(find $(ROOT)/std -name '*.c6' | sort); do \
	  if $(BUILD)/c6c --scan $$f > /dev/null 2>&1; then ok=$$((ok + 1)); \
	  else bad=$$((bad + 1)); echo "扫描失败：$$f"; fi; \
	done; \
	echo "c6c: 预处理+词法 通过 $$ok / 失败 $$bad"; \
	[ $$bad -eq 0 ]

# std/ 153 个模块（在仓库根目录）：走完整前端（-fsyntax-only）。
# 如实报通过数——c6 的扩展语法（错误通道 T!error、test 块、foreach/match、
# 切片）还没实现，所以现在不会全绿。
stdtest: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ROOT=$(ROOT) ./tests/std-tests.sh -j $(JOBS)

bench:
	@test -x $(C6C) || { \
	  echo "c6c: bench 拿 $(C6C) 当基线，它不存在（BOOTSTRAP=host / 无 tcc 时不适用）" >&2; \
	  echo "c6c: 改用 make bench-c6c（只量 c6c 自己）" >&2; exit 1; }
	$(PYTHON) bench/bench.py --root $(ROOT) --cc $(C6C) --runs $(RUNS) \
	    --json bench/last.json

# 性能口径的测量二进制：gcc -O3（P2-2 之后换成 c6c 自己编出来的 c6c）。
# 为什么不是 $(BUILD)/c6c：那是 tcc 编的，tcc 的代码生成比 gcc 差 4.3 倍
# （docs/design.md §7 的 G4b 实测），拿它量《135 ms》等于在量 tcc 的寄存器分配。
#
# 为什么是 -O3 而不是 -O2：两个都量过，同一份源码 tcc.c 全编译
# -O2 138 ms / -O3 132 ms（门槛 135）。发布构建本来就该用 -O3，两个数字都
# 记在 bench/last.json 和 docs/todo.md 里，没有藏着。
OPTFLAGS ?= -O3
$(BUILD)/c6c-opt: $(SRCS)
	@mkdir -p $(BUILD)
	@gcc -x c $(OPTFLAGS) -o $@ src/c6c.c6
	@echo "c6c: $@（gcc $(OPTFLAGS) 测量二进制）"

bench-c6c: $(BUILD)/c6c-opt
	@echo "bench: 测量二进制 = $(BUILD)/c6c-opt（gcc $(OPTFLAGS)）"
	$(PYTHON) bench/bench.py --root $(ROOT) --cc $(abspath $(BUILD)/c6c-opt) \
	    --runs $(RUNS) --assert --json bench/last.json

# 内存内数据结构自测：类型表（P3-1）+ IR（P2-1）
unit: $(BUILD)/c6c
	@$(BUILD)/c6c --type-selftest
	@$(BUILD)/c6c --ir-selftest

# IR v1（P2-1）：编解码往返 + dump 稳定（两次运行逐字节相同）
irtest: $(BUILD)/c6c
	@$(BUILD)/c6c --ir-selftest
	@$(BUILD)/c6c --ir-demo > $(BUILD)/ir-demo-1.txt
	@$(BUILD)/c6c --ir-demo > $(BUILD)/ir-demo-2.txt
	@cmp $(BUILD)/ir-demo-1.txt $(BUILD)/ir-demo-2.txt
	@echo "c6c: IR dump 稳定（两次运行逐字节相同）"

# P2-2 骨架：c6c 自己发的 ELF 必须能被内核装载，退出码 = IR 里的常量
hello: $(BUILD)/c6c
	@$(BUILD)/c6c --hello $(BUILD)/hello
	@$(BUILD)/hello; rc=$$?; \
	  if [ $$rc -ne 42 ]; then echo "c6c: hello 退出码 $$rc（应为 42）"; exit 1; fi; \
	  echo "c6c: hello 退出码 42（IR -> x86-64 -> ELF -> 内核装载，不经 tcc/gcc/libc）"

# 前端/后端回归（P3-2/P3-3 的当前子集）：gcc 当 oracle 比对退出码
ctest: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ./tests/run.sh

# -run：编到临时 ELF 再 exec。退出码/参数/test 块/临时文件清理/模式互斥
runflag: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ./tests/run-flag.sh

# -test：跑 test 块、不跑用户 main。PASS/FAIL/收尾统计的输出协议、
# -test-filter、失败数当退出码、零块静默——和 tcc 的 native 运行器同一口径。
testflag: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ./tests/test-flag.sh

# tcc 上游 tests2 记分板（目标 3 的度量尺）
tcc-tests: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ./tests/tcc-tests2.sh -j $(JOBS)

# -c：出 ET_REL 目标文件。单/多 TU 都真编、真用系统链接器链、真跑，
# 再用 readelf 核对"外部调用是 PLT32 + UND、数据里的地址是 R_X86_64_64"。
objcheck: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ./tests/obj-check.sh

# 库形态：把 c6c 编成能嵌进别人程序的编译器（src/embed.c6 的 c6_* API）。
# 同一份源码，用 -DC6C_NO_MAIN 关掉 main()；lib/rt.c6 与 include/ 的绝对路径
# 在编译期塞进去（库形态没有 argv[0] 可推，见 src/embed.c6）。
LIBC6C_DEFS = -DC6_DEFAULT_RT='"$(abspath lib/rt.c6)"' -DC6_DEFAULT_INC='"$(abspath include)"'

libc6c:
	@mkdir -p $(BUILD)
	$(HOSTCC) -x c $(HOSTCFLAGS) -fPIC $(LIBC6C_DEFS) -c -o $(BUILD)/libc6c.o src/c6c-lib.c6
	ar rcs $(BUILD)/libc6c.a $(BUILD)/libc6c.o
	$(HOSTCC) -x c $(HOSTCFLAGS) -fPIC -shared $(LIBC6C_DEFS) -o $(BUILD)/libc6c.so src/c6c-lib.c6
	@echo "c6c: $(BUILD)/libc6c.a + $(BUILD)/libc6c.so（嵌入 API：include/c6lib.h）"

# 嵌入 API 的回归：编一个宿主程序链上 libc6c，把 c6 源码编进内存里跑
embedcheck: libc6c
	@C6C_LIB=$(abspath $(BUILD)) C6C_INC=$(abspath include) ./tests/embed-check.sh

# 库形态与安装契约：C6_ENABLE_TCC=OFF 的安装树 + find_package + pkgconf
pkgcheck: libc6c
	@./tests/pkgcheck.sh

# -S（只出汇编文本）的回归：c6c → .s → 系统 as → 真跑，退出码要和 native 一致
asmcheck: build
	@C6C=$(abspath $(BUILD)/c6c) ./tests/asmcheck.sh

# 跨目标：c6c 的 IR 经 llc 落到 aarch64，在 qemu 上真跑（缺 llc/clang/ld.lld/
# qemu-aarch64 就自己跳过；它验的是"IR 与目标无关"，不是"c6c 有 aarch64 后端"）
archcheck: build
	@C6C=$(abspath $(BUILD)/c6c) ./tests/archcheck.sh

# -g（调试信息）的回归：DWARF 段 + 行号表 + addr2line/gdb 认得出来
dbgcheck: build
	@C6C=$(abspath $(BUILD)/c6c) ./tests/dbgcheck.sh

# -b（数组下标边界检查）的回归：越界写/读要拦住，界内访问不受影响
bcheck: build
	@C6C=$(abspath $(BUILD)/c6c) ./tests/bcheck.sh

# 动态链接的回归：-Wl,--dynamic 出 DT_NEEDED libc.so.6 的可执行文件，真调 libc
dyncheck: build
	@C6C=$(abspath $(BUILD)/c6c) ./tests/dyn-check.sh

# 自己的链接器（src/ld.c6）：.o → 可执行文件。纯自足的两个 .o、真实规模的
# rt.o（564 条重定位）、构造函数顺序、跨 .o 的 TLS 记账格、缺符号报错。
ldcheck: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ./tests/ld-check.sh

lint:
	./scripts/lint.sh

# 自举 fixpoint（验收第 1 条的核心）：c6c 编自己 → c6c2，c6c2 再编自己 → c6c3，
# 两份二进制必须逐字节相同。
selfhost: $(BUILD)/c6c
	@$(BUILD)/c6c -o $(BUILD)/c6c2 src/c6c.c6
	@C6C_RT=$(abspath lib/rt.c6) $(BUILD)/c6c2 -I$(abspath include) \
	    -o $(BUILD)/c6c3 src/c6c.c6
	@cmp $(BUILD)/c6c2 $(BUILD)/c6c3
	@echo "c6c: 自举 fixpoint 逐字节相同（c6c2 == c6c3，$$(stat -c%s $(BUILD)/c6c2) 字节）"

# 自测：c6c 编自己 → 编出来的编译器还要能通过它自己的内存数据结构自测。
# 和 selfhost 的区别：selfhost 比的是「逐字节 fixpoint」，这里比的是
# 「自己产的编译器功能上也一样好」。
selftest: $(BUILD)/c6c
	@$(BUILD)/c6c -o $(BUILD)/c6c-self src/c6c.c6
	@$(BUILD)/c6c-self --type-selftest
	@$(BUILD)/c6c-self --ir-selftest
	@echo "c6c: 自举产出的编译器通过自带自测（类型表 + IR）"

# tests/ 下的 c6 用例（c6 扩展语法：错误通道、test 块、切片…）走完整前端。
# 如实报通过数，和 stdtest 同一个口径。
# 两把尺子：前端能过、test 块能跑（见 tests/run-tests.sh）
# 每个用例是独立的编译器进程，`JOBS=N`（默认 CPU 数）并行跑；并发/协程那批
# 对机器负载敏感，驱动里单独走串行那一遍，结论和 -j 1 完全一样。
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
test: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ROOT=$(ROOT) ./tests/run-tests.sh -j $(JOBS)

# 编译期诊断用例（tests/errors/*.c6 里每条 `// EXPECT`）：报告用
# `make -s diagtest`，逐条明细加 `V=1`。
diagtest: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ROOT=$(ROOT) ./tests/diag-tests.sh -j $(JOBS) $(if $(V),-v)

# 目标（2）的 G2c：用 c6c 编出 tcc 本体，再由这个 tcc 跑上游 tests2。
#
# 这一步编的是 tcc 的**源码**（语料），而那份源码要用三个**生成**物：
#   config.h / config.mak —— tcc/CMakeLists.txt 生成（tcc-config 目标）
#   tccdefs_.h            —— tcc/Makefile 的规则：`cc -DC2STR tcc/conftest.c`
#                            出 c2str.exe，再由它把 include/tccdefs.h 转成
#                            tccdefs_.h
#   libtcc1.a             —— tcc 的 C 运行库。它**必须由能用的 tcc 编**：上游
#                            允许 `x86_64-libtcc1-usegcc=yes` 用宿主 cc 编，
#                            但实测那样编出来的运行时会让 c6c 编出的 tcc 少过
#                            一批用例（102/144、第三代 selfhost 坏 8），而用
#                            tcc 编的是 118/144、坏 0（与 tcc 自己构建的运行时
#                            逐条一致）。tcc/ 里还没有 c6 时，这里先用宿主 cc
#                            把 tcc 源码编成一个**参考 tcc**（`make -C tcc c6`，
#                            和产品构建同一条 cc 编译链，只是不经过 CMake、
#                            不安装、不进任何交付物），再由它编 libtcc1.a。
#                            这一步同时带 EXTRA-DEFS=-DC6_EXT：tcc/Makefile 里
#                            写得很清楚，tcc.h 的 struct TCCState 按这个宏开合
#                            字段，运行时与编译器两侧不一致就是静默错编译。
# 三个都不需要**产品构建**的 tcc（一个靠 cmake，另外两个靠宿主 cc），
# 所以纯 Makefile 流程——BOOTSTRAP=host、根本没走过 CMake 的检出——也能把
# 语料备齐，然后由 c6c 编出的那个 tcc 真的跑起 tests2。
# 已经由 tcc 的 CMake 构建备好时这三步都是空操作。
# 语料运行时用的 EXTRA-DEFS，与产品构建的默认（C6_ENABLE_EXT=ON）一致。
# 单独一行赋值：行尾注释会把注释前的空格留在值里，而这个值是用引号传下去的。
C6EXT ?= -DC6_EXT
TCC_CONFIG_STAMP := $(BUILD)/tcc-config.stamp
$(TCC_CONFIG_STAMP):
	@mkdir -p $(BUILD)
	@if [ ! -f "$(ROOT)/tcc/config.h" ]; then \
	  echo "c6c: 生成 tcc 的配置头（只跑 tcc-config，不编 tcc）"; \
	  "$(CMAKE)" -S "$(ROOT)/tcc" -B "$(BUILD)/tcc-config" > /dev/null && \
	  "$(CMAKE)" --build "$(BUILD)/tcc-config" --target tcc-config > /dev/null; \
	fi
	@if [ ! -f "$(ROOT)/tcc/tccdefs_.h" ]; then \
	  echo "c6c: 生成 tcc/tccdefs_.h（tcc/Makefile 的规则，用宿主 cc）"; \
	  $(MAKE) -C "$(ROOT)/tcc" tccdefs_.h EXTRA-DEFS="$(C6EXT)" > /dev/null; \
	fi
	@if [ ! -f "$(ROOT)/tcc/libtcc1.a" ]; then \
	  if [ ! -x "$(ROOT)/tcc/c6" ]; then \
	    echo "c6c: 用宿主 cc 编一个参考 tcc（G2c 的运行时夹具，不安装）"; \
	    $(MAKE) -C "$(ROOT)/tcc" c6 EXTRA-DEFS="$(C6EXT)" > /dev/null; \
	  fi; \
	  echo "c6c: 用参考 tcc 编 tcc 的 C 运行库 libtcc1.a"; \
	  $(MAKE) -C "$(ROOT)/tcc/lib" EXTRA-DEFS="$(C6EXT)" > /dev/null; \
	fi
	@touch $@

build-tcc: $(BUILD)/c6c-opt $(TCC_CONFIG_STAMP)
	@mkdir -p $(BUILD)
	@$(BUILD)/c6c-opt -o $(BUILD)/tcc-c6c -DONE_SOURCE=1 \
	    -I$(ROOT)/tcc -I$(abspath include) $(ROOT)/tcc/tcc.c
	@echo "c6c: $(BUILD)/tcc-c6c（tcc 由 c6c 编出）"
	@C6C_TCC_SELF_MIN=$(C6C_TCC_SELF_MIN) ./tests/tcc-self-check.sh \
	    $(abspath $(BUILD)/tcc-c6c)

# 三后端（native / C99 / LLVM IR）跑同一套用例，各比标准输出 + 退出码。
# C99 后端：同一份 C 源，native 和 C99 各编一遍，比标准输出 + 退出码
c99test: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c CC=$(CC) CORPUS=$(CURDIR)/tests/c ./tests/c99-diff.sh

# LLVM IR 后端：同一份 C 源，native 和 LLVM IR 各编一遍（llc + cc 出可执行），
# 比标准输出 + 退出码
# llc 常常装在 /usr/lib/llvm-NN/bin 而不是 PATH 里：先按 PATH 找，找不到再
# 挑一个装好的版本（不然 llvmtest 全挂在链接阶段，看不出后端好坏）。
LLVM_LLC ?= $(shell command -v llc 2>/dev/null || ls -d /usr/lib/llvm-*/bin/llc 2>/dev/null | sort -V | tail -1)
# 验收（2）的后半段：**c6c 编出来的 tcc** 跑它自己的 tests2（记分板，不挡
# check-all —— 上游有一批交叉目标/要 libtcc1.a 的用例在 x86-64 上本来就跑不了）。
g2c-check: $(BUILD)/tcc-c6c
	@TCC=$(BUILD)/tcc-c6c ROOT=$(ROOT) ./tests/g2c-tests2.sh

# 目标（2）（3）的总记分板：tests2 / tests1 / g2c 三把尺子各一行。
scoreboard: $(BUILD)/c6c
	@ROOT=$(ROOT) C6C=$(BUILD)/c6c ./tests/scoreboard.sh -j $(JOBS)

# 验收（3）的正主：c6c 直接跑 tcc 上游 tests2。
# `tests/tcc-na.txt` 里登记的用例算"架构性不可达"（每条都写了具体卡点），
# 不登记的一律算意外失败 —— 所以这条**可以当闸门**，不是进度条。
tests2-check: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ROOT=$(ROOT) ./tests/tcc-tests2.sh -j $(JOBS)

# 验收（3）的另一半：tcc 上游 tests/ 下那批"老式"单文件用例（tests1 口径）。
# 记分板，不进 check-all（多数要 tcc 自己的 libtcc1.a / tcc.h 内部头）。
tests1-check: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ROOT=$(ROOT) ./tests/tcc-tests1.sh

# 语言层 C99 的外部闸门：c-testsuite（third_party/c-testsuite/，scc 来源 150 条）。
# 口径是"语言 100% 兼容 C99，库不管"：LANG（前端编不过）与 OUT（输出不符）必须为 0，
# LIB（链接期缺符号，运行期没实现）单独计数、不挡闸门；超出口径的用例（C11 才有的
# 语言特性）登记在 tests/c99-out-of-scope.txt，单列一类 N/A。LANG+OUT 已经清零，
# 所以它现在是 check-all 的一部分。
c99suite: $(BUILD)/c6c
	@C6C=$(BUILD)/c6c ROOT=$(ROOT) ./tests/c99-suite.sh -j $(JOBS)

llvmtest: $(BUILD)/c6c
	@echo "c6c: llvmtest 用 LLC=$(LLVM_LLC) CC=$(CC)"
	@C6C=$(BUILD)/c6c LLC=$(LLVM_LLC) CC=$(CC) CORPUS=$(CURDIR)/tests/c ./tests/llvm-diff.sh

# 快速门禁（实测 ≈40 s）：改一行代码先跑这条。闸门口径和 check-all 完全一致，
# 只是把"重复跑一整遍全量编译/全量语料"的那几条挪出去：
#   selfhost + selftest   自举 fixpoint（两遍 ~11 MB 全量编译）
#   stdtest               377 个 std 模块
#   test + diagtest       主仓 120+ 用例与 316 条编译期诊断
#   c99test               C99 后端差分，51 条要 165 s（每条 gcc -O0 编两次）
#   build-tcc             c6c 编出上游 tcc，再由它跑上游 tests2 + 第三代 selfhost
# 留下的都是秒级、确定性的闸门，而且**外部语料是它们的一部分**，不是可选项：
#   c99suite      third_party/c-testsuite 的 150 条外部 C99 用例（语言层闸门）
#   tests2-check  tcc 上游 tests2 逐条比对（tests/tcc-na.txt 之外不许有意外失败）
#   tests1-check  tcc 上游 tests/ 那批老式单文件用例
# llvmtest 留在这里：26 s 换一条确定性的后端闸门（同一份语料 native vs LLVM）。
# CMake 侧的 `cmake --build build --target fast` 就是这条（c6c 替换 c6 的快车道）。
fast: lint unit irtest hello ctest runflag testflag objcheck ldcheck dyncheck bcheck dbgcheck asmcheck archcheck embedcheck pkgcheck tests2-check tests1-check c99suite llvmtest
	@echo "c6c: fast 通过（快速门禁 ≈40 s：前端/IR/ELF 骨架 + -run/-test/-c/链接 + 外部语料 c99suite / 上游 tests1·tests2 + LLVM 后端对齐）"

# 验收（1）(2) (3)：功能面。跑分不在里面——验收（4）那几条硬指标（自编译
# ≤100 ms、峰值 RSS ≤32 MB、4x 输入 ≤2.6x）随时可能因为机器负载抖动，
# 拿它挡功能验收只会让人学会忽略红灯。要连跑分一起验就用 check-all-perf。
# 顺序有讲究：`llvmtest` 放前面。它是**确定性**的后端闸门（同一份语料三方对齐），
# 而 `test` 里有个别用例（`async`）在共享 runner 上偶发段错误——先跑确定的那条，
# CI 红了至少能一眼看出是不是后端真的坏了。
# 上游 tests1/tests2 两条闸门不在这个依赖里（它们是独立的记分板入口），
# CMake 侧 `cmake --build build --target check-all-c6c` 会把它们一起跑完。
check-all: unit irtest hello ctest runflag testflag objcheck ldcheck dyncheck bcheck dbgcheck asmcheck archcheck embedcheck pkgcheck selfhost selftest stdtest diagtest c99test c99suite llvmtest test build-tcc
	@echo "c6c: check-all 通过（验收 1~3：自举 fixpoint / 三后端 / tests+std+诊断 / ET_REL + 链接器 + 动态链接 + -g/-b/-S + 嵌入库 / 语言层 C99 外部闸门 / 能编出 tcc）"

# 验收（4）：性能与内存硬指标（门槛在 bench/thresholds.json）
check-all-perf: check-all bench-c6c
	@echo "c6c: check-all-perf 通过（含性能/内存硬指标）"

help:
	@sed -n '1,11p' $(MAKEFILE_LIST) | sed 's/^# \{0,1\}//'
