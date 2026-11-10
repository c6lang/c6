# c6 —— 带语言扩展的 TinyCC
#
#   make bootstrap   准备构建（tcc/ 源码已在仓库里；缺 config.mak 时跑 configure）
#   make build       以扩展模式构建（EXTRA-DEFS=-DC6_EXT）
#   make build-plain 以上游模式构建（不定义 C6_EXT）
#   make selftest    跑 tests/ 下所有 c6 自测（test 块 + 编译期诊断）
#   make test        跑 tests2 上游全量测试（扩展模式）
#   make test-plain  关闭扩展构建并跑同一套上游测试
#   make test-all    selftest + tests2 全量
#   make c99         C99 输出后端已内置在 tcc/tcc（-c99 / -o *.c / -o <目录>）
#   make c99test     与 selftest 相同的用例走 C99 后端（生成 C，再用 gcc 编译运行）
#   make c99tcc-test 同一批用例，但生成 C 的宿主编译器换成 tcc 自己（不依赖 gcc）
#   make c6selfcheck tcc 自己的源码拷成 .c6 后用 c6 规则编译（初始化规则 + C99 后端）
#   make selfhost    用 tcc 自举自己（stage1 -> stage2，比对目标文件）
#   make c99selfhost 用 C99 后端自举自己（生成 C -> gcc -> stage2 -> stage3）
#   make c99selfhost-tcc 同上，但生成 C 之后由 tcc 自己编译（整条链不需要 gcc）
#   make clean       清理 tcc/ 的构建产物和自举产物
#   make distclean   清理 tcc/ 的构建产物和 configure 结果（不删源码）
#
# C6_EXT 是编译期开关，切换模式时会自动先清理旧产物。

TCC_DIR  := tcc
SELFTEST := tests/run-tests.sh
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

.PHONY: all bootstrap build build-plain test test-plain test-all selftest \
        clean distclean help check-tcc mode-ext mode-plain c99 c99test \
        c99tcc-test c6selfcheck selfhost c99selfhost c99selfhost-tcc

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

# 无 libc（freestanding）冒烟测试：不链接 crt / libc，入口是 _start
fstest: build
	./tcc/tcc -B tcc -static -nostdlib -o build/freestanding_fstr \
	    examples/freestanding_fstr.c6 tcc/libtcc1.a
	./build/freestanding_fstr

# freestanding + 程序自己实现的 pthread 层：go / thread
fstest-go: build
	./tcc/tcc -B tcc -c -o build/freestanding_pthread.o \
	    examples/freestanding_pthread.c
	./tcc/tcc -B tcc -static -nostdlib -o build/freestanding_go \
	    examples/freestanding_go.c6 build/freestanding_pthread.o tcc/libtcc1.a
	./build/freestanding_go

# 完全没有操作系统：程序里一个 syscall 都没有，靠 ud2 / 踩空报结果
fstest-noos: build
	./tcc/tcc -B tcc -static -nostdlib -o build/freestanding_noos \
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

test-all: selftest test c6selfcheck fstest fstest-go fstest-noos fstest-qemu \
	  fstest-bare fstest-bare-tests fstest-bare-go fstest-bare-smp

# C99 输出后端已内置在 tcc/tcc 里：同一份二进制用 -c99（或 -o *.c、
# -o <目录>）产出 C 源码。保留这个目标只是为了兼容旧脚本。
c99: build
	@echo "c99: C99 输出已内置在 $(TCC_DIR)/tcc，无需单独构建"

# C99 输出后端的端到端测试：与 selftest 相同的测试用例，但每个源文件先
# 由同一个 tcc 生成 C 源码，再由系统 gcc 编译运行。
c99test: build
	BACKEND=c99 ./$(SELFTEST)

# 与 c99test 完全同一批用例，只是编译生成代码的宿主编译器换成 tcc 自己：
# 生成的文件必须能被 tcc 编过（没有 GNU 的 alias 属性，方法名靠生成文件里
# 的转发函数导出，va_list 助手用私有名字，见 tcc/c99-gen.c）。
c99tcc-test: build
	BACKEND=c99 C99CC=$(CURDIR)/$(TCC_DIR)/tcc \
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
	@out=`$(TCC_DIR)/tcc -B $(TCC_DIR) -I $(TCC_DIR) $(SELFCHECK_DEFS) -c $(SELFHOST_C6)/selfcheck.c6 -o $(SELFHOST_C6)/selfcheck.o 2>&1`; \
	if [ -n "$$out" ]; then \
	    printf '%s\n' "$$out" >&2; \
	    echo "c6selfcheck: 用 tcc 编译自身时不应有任何诊断输出（上面这些就是）" >&2; \
	    exit 1; \
	fi
	@out=`$(TCC_DIR)/tcc -B $(TCC_DIR) -I $(TCC_DIR) $(SELFCHECK_DEFS) -o $(SELFHOST_C6)/selfcheck.c $(SELFHOST_C6)/selfcheck.c6 2>&1`; \
	if [ -n "$$out" ]; then \
	    printf '%s\n' "$$out" >&2; \
	    echo "c6selfcheck: 用 tcc 生成 C 时不应有任何诊断输出（上面这些就是）" >&2; \
	    exit 1; \
	fi
	$(HOSTCC) -std=c99 -c -o $(SELFHOST_C6)/selfcheck-c99.o $(SELFHOST_C6)/selfcheck.c
	$(TCC_DIR)/tcc -B $(TCC_DIR) -o $(SELFHOST_C6)/selfcheck $(SELFHOST_C6)/selfcheck.o $(SELFHOST_LIBS)
	$(SELFHOST_C6)/selfcheck -v

# 自举（c6 模式）：tcc 用它自己的后端编译自己的源码，再用编出来的编译器
# 重编一遍。两次的目标文件必须逐字节相同（bootstrap fixpoint），最后用
# stage2 编一个程序跑通，确认它不只是"能启动"。
selfhost: build
	@mkdir -p $(SELFHOST_C6)
	@echo "c6 自举 1/4：$(TCC_DIR)/tcc 编译自己的源码"
	$(TCC_DIR)/tcc -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C6)/stage1.o
	$(TCC_DIR)/tcc -B $(TCC_DIR) -o $(SELFHOST_C6)/tcc-stage1 $(SELFHOST_C6)/stage1.o $(SELFHOST_LIBS)
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
	$(TCC_DIR)/tcc -B $(TCC_DIR) $(SELFHOST_DEFS) -c99 -o $(SELFHOST_C99)/tcc-c99.c $(TCC_DIR)/tcc.c
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
	$(TCC_DIR)/tcc -B $(TCC_DIR) $(SELFHOST_DEFS) -c99 -o $(SELFHOST_C99TCC)/tcc-c99.c $(TCC_DIR)/tcc.c
	@echo "c99+tcc 自举 2/4：同一个 tcc 编译生成的 C，得到 stage2 编译器"
	$(TCC_DIR)/tcc -B $(TCC_DIR) -o $(SELFHOST_C99TCC)/tcc-stage2 $(SELFHOST_C99TCC)/tcc-c99.c $(SELFHOST_LIBS)
	@echo "c99+tcc 自举 3/4：stage2 编一遍 tcc.c，tcc 自己也编一遍"
	$(SELFHOST_C99TCC)/tcc-stage2 -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C99TCC)/stage3.o
	$(TCC_DIR)/tcc -B $(TCC_DIR) $(SELFHOST_DEFS) -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C99TCC)/native.o
	@echo "c99+tcc 自举 4/4：比较两个目标文件"
	@cmp $(SELFHOST_C99TCC)/native.o $(SELFHOST_C99TCC)/stage3.o && echo "  逐字节相同"
	@printf '%s\n' '#include <stdio.h>' 'int main(void)' '{' '    printf("hello from the tcc-hosted c99 self-hosted tcc\n");' '    return 0;' '}' > $(SELFHOST_C99TCC)/hello.c
	$(SELFHOST_C99TCC)/tcc-stage2 -B $(TCC_DIR) $(SELFHOST_C99TCC)/hello.c -o $(SELFHOST_C99TCC)/hello
	@$(SELFHOST_C99TCC)/hello
	@echo "c99+tcc 自举完成：$(SELFHOST_C99TCC)/tcc-stage2"

clean:
	rm -rf $(SELFHOST)
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) clean || true

distclean:
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) distclean || true

help:
	@sed -n '2,18p' Makefile
