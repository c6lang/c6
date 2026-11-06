#!/bin/sh
# 构建前准备：tcc/ 已经是本仓库的源码（上游 TinyCC 基线 2d3eea2，之后每个
# c6 补丁一个提交），不再克隆上游、也不再打补丁。这里只剩一件事：
# 还没有配置过（缺 config.mak）时跑一次 ./configure，生成构建配置。
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

if [ -f "$TCC_DIR/config.mak" ]; then
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
  ./tcc/tcc -B tcc -std=c6 -run hello.c   # 在仓库根目录运行

EOF
