# c6 —— 带语言扩展的 TinyCC
#
#   make bootstrap   生成 tcc/ 工作树（克隆上游 + 应用 patches/）
#   make build       以扩展模式构建（EXTRA-DEFS=-DC6_EXT）
#   make build-plain 以上游模式构建（不定义 C6_EXT）
#   make selftest    跑 tests/ 下所有 c6 自测（test 块 + 编译期诊断）
#   make test        跑 tests2 上游全量测试（扩展模式）
#   make test-plain  关闭扩展构建并跑同一套上游测试
#   make test-all    selftest + tests2 全量
#   make clean       清理 tcc/ 的构建产物
#   make distclean   连 tcc/ 工作树一起删除
#
# C6_EXT 是编译期开关，切换模式时会自动先清理旧产物。

TCC_DIR  := tcc
SELFTEST := tests/run-tests.sh
JOBS     ?= $(shell nproc 2>/dev/null || echo 4)
CFLAGS   ?= -g -O0 -Wall -Wno-unused-result
EXT      := EXTRA-DEFS=-DC6_EXT
MODE     := $(TCC_DIR)/.c6-mode

.PHONY: all bootstrap build build-plain test test-plain test-all selftest \
        clean distclean help check-tcc mode-ext mode-plain

all: build

bootstrap:
	@if [ -d $(TCC_DIR)/.git ]; then \
	    echo "c6: $(TCC_DIR)/ 已存在，跳过（需要重建请先 make distclean）"; \
	else \
	    ./scripts/bootstrap.sh; \
	fi

check-tcc:
	@test -d $(TCC_DIR)/.git || { echo "缺少 $(TCC_DIR)/，先执行 'make bootstrap'"; exit 1; }

# 记录当前构建模式，模式变化时清理，避免复用错误配置的目标文件
mode-ext: check-tcc
	@if [ "$$(cat $(MODE) 2>/dev/null)" != "ext" ]; then \
	    echo "c6: 切换到扩展模式（清理旧产物）"; \
	    $(MAKE) -C $(TCC_DIR) clean >/dev/null 2>&1 || true; \
	    echo ext > $(MODE); \
	fi

mode-plain: check-tcc
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

clean:
	@test -d $(TCC_DIR) && $(MAKE) -C $(TCC_DIR) clean || true

distclean:
	rm -rf $(TCC_DIR)

help:
	@sed -n '2,15p' Makefile
