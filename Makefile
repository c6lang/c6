# c6 —— 带语言扩展的 TinyCC
#
#   make bootstrap   准备构建（tcc/ 源码已在仓库里；缺 config.mak 时跑 configure）
#   make build       以扩展模式构建（EXTRA-DEFS=-DC6_EXT）
#   make build-plain 以上游模式构建（不定义 C6_EXT）
#   make selftest    跑 tests/ 下所有 c6 自测（test 块 + 编译期诊断）
#   make test        跑 tests2 上游全量测试（扩展模式）
#   make test-plain  关闭扩展构建并跑同一套上游测试
#   make test-all    selftest + tests2 全量
#   make c99         构建 C99 输出后端 tcc/c99-tcc（产出可移植 C 源码）
#   make clean       清理 tcc/ 的构建产物
#   make distclean   清理 tcc/ 的构建产物和 configure 结果（不删源码）
#
# C6_EXT 是编译期开关，切换模式时会自动先清理旧产物。

TCC_DIR  := tcc
SELFTEST := tests/run-tests.sh
JOBS     ?= $(shell nproc 2>/dev/null || echo 4)
CFLAGS   ?= -g -O0 -Wall -Wno-unused-result
EXT      := EXTRA-DEFS=-DC6_EXT
MODE     := $(TCC_DIR)/.c6-mode

.PHONY: all bootstrap build build-plain test test-plain test-all selftest \
        clean distclean help check-tcc mode-ext mode-plain c99

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
selftest: build
	./$(SELFTEST)

test-all: selftest test

# C99 输出后端：单源构建的 tcc/c99-tcc（自带 -DC6_EXT），只产出 C 源码。
c99: bootstrap
	$(MAKE) -C $(TCC_DIR) c99-tcc

clean:
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) clean || true

distclean:
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) distclean || true

help:
	@sed -n '2,16p' Makefile
