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
#   make c6selfcheck tcc 自己的源码用 -std=c6 编译（初始化规则 + C99 后端）
#   make selfhost    用 c6 模式的 tcc 自举自己（stage1 -> stage2，比对目标文件）
#   make c99selfhost 用 C99 后端自举自己（生成 C -> gcc -> stage2 -> stage3）
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
# 自举时编译 tcc 自己用的定义：ONE_SOURCE 把整个编译器塞进一个编译单元，
# C6_EXT 打开 c6 扩展（与 make build 的 EXTRA-DEFS 一致）
SELFHOST_DEFS := -DONE_SOURCE=1 -DC6_EXT
SELFHOST_LIBS := -lm -ldl -lpthread
HOSTCC   ?= gcc

.PHONY: all bootstrap build build-plain test test-plain test-all selftest \
        clean distclean help check-tcc mode-ext mode-plain c99 c99test \
        c6selfcheck selfhost c99selfhost

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
#   tests/*.c          用 -test 跑里面的 test 块（同名 tests/tu/*.c 作为第二 TU）
#   tests/errors/*.c   编译期诊断，见 tests/run-tests.sh
#   tests/c99/*.c      C99 输出后端：先生成 C 源码，再用 gcc 编译运行
selftest: build
	./$(SELFTEST)

test-all: selftest test c6selfcheck

# C99 输出后端已内置在 tcc/tcc 里：同一份二进制用 -c99（或 -o *.c、
# -o <目录>）产出 C 源码。保留这个目标只是为了兼容旧脚本。
c99: build
	@echo "c99: C99 输出已内置在 $(TCC_DIR)/tcc，无需单独构建"

# C99 输出后端的端到端测试：与 selftest 相同的测试用例，但每个源文件先
# 由同一个 tcc 生成 C 源码，再由系统 gcc 编译运行。
c99test: build
	BACKEND=c99 ./$(SELFTEST)

# c6 语言模式自检：tcc 自己的源码也必须满足 "变量必须初始化"，所以拿
# -std=c6 编译一遍自己的源码是这条规则最严格的回归测试（同时覆盖 C99
# 后端的翻译路径）；编出来的编译器还要能跑。
c6selfcheck: build
	@mkdir -p $(SELFHOST_C6)
	$(TCC_DIR)/tcc -B $(TCC_DIR) -std=c6 -c $(TCC_DIR)/tcc.c -o $(SELFHOST_C6)/selfcheck.o
	$(TCC_DIR)/tcc -B $(TCC_DIR) -std=c6 -o $(SELFHOST_C6)/selfcheck.c $(TCC_DIR)/tcc.c
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

clean:
	rm -rf $(SELFHOST)
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) clean || true

distclean:
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) distclean || true

help:
	@sed -n '2,16p' Makefile
