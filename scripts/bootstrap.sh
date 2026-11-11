#!/bin/sh
# 构建前准备：tcc/ 已经是本仓库的源码（上游 TinyCC 基线 2d3eea2，之后每个
# c6 补丁一个提交），不再克隆上游、也不再打补丁。这里只剩一件事：还没配置
# 过、或配置比 ./configure 旧（configure 被本仓库改过，例如新增 C99 目标）
# 时跑一次 ./configure，生成构建配置。
#
# 改 TCC 源码就直接改 tcc/ 里的文件，按普通提交走；不存在“重新同步工作树”
# 这回事，所以手改不会被覆盖。
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TCC_DIR="$ROOT/tcc"

if [ ! -f "$TCC_DIR/tccpp.c" ]; then
    echo "bootstrap: 缺少 $TCC_DIR/ 源码，检出可能不完整。" >&2
    exit 1
fi

# config.mak/config.h 由 ./configure 生成，里面的目标列表（TCC_TARGET_*）
# 是 configure 写死的。本仓库会改 configure（例如加 TCC_TARGET_C99），
# 旧配置不认识新目标，构建时就会退回到默认目标、报出莫名其妙的错误。
# 因此出现下面任一情况都重新生成一次：
#   - configure 比 config.mak 新（configure 只在内容变化时才重写 config.h，
#     所以时间戳以每次都会重写的 config.mak 为准）；
#   - config.h 里没有 C99/LLVM 目标（时间戳不可靠时也能识别出旧配置）。
if [ -f "$TCC_DIR/config.mak" ] && [ -f "$TCC_DIR/config.h" ] \
   && [ ! "$TCC_DIR/configure" -nt "$TCC_DIR/config.mak" ] \
   && grep -q TCC_TARGET_C99 "$TCC_DIR/config.h" \
   && grep -q TCC_TARGET_LLVM "$TCC_DIR/config.h"; then
    echo "bootstrap: $TCC_DIR 已配置"
    exit 0
fi

echo "bootstrap: 配置 $TCC_DIR（./configure --prefix=tcc/dist）"
( cd "$TCC_DIR" && ./configure --prefix="$TCC_DIR/dist" >/dev/null )

cat <<EOF

bootstrap: 完成。下一步：

  make build        # 打开扩展构建（EXTRA-DEFS=-DC6_EXT）
  make selftest     # 跑 tests/ 下的 c6 自测
  make test         # 跑上游 tests2 全量测试
  ./tcc/c6 -B tcc -run hello.c6  # c6 代码放进 .c6 文件，后缀就是开关

EOF
