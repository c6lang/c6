#!/bin/sh
# 生成 c6 的 TCC 工作树：
#   1. 克隆上游 TinyCC；
#   2. 检出固定基线（BASE_COMMIT）；
#   3. 依次应用 patches/*.patch；
#   4. 运行 ./configure 生成 config.mak。
#
# 可用环境变量：
#   C6_TCC_URL   上游仓库地址（默认 GitHub；离线时可指向本地克隆）
#   C6_TCC_BASE  基线提交（默认见下）
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TCC_DIR="$ROOT/tcc"
BASE_COMMIT=${C6_TCC_BASE:-2d3eea2}
UPSTREAM=${C6_TCC_URL:-https://github.com/TinyCC/tinycc.git}

if [ -d "$TCC_DIR/.git" ]; then
    echo "bootstrap: $TCC_DIR 已存在，跳过。需要重建请先执行 'make distclean'。"
    exit 0
fi
if [ -e "$TCC_DIR" ]; then
    echo "bootstrap: $TCC_DIR 存在但不是 git 工作树，请先删除或改名。" >&2
    exit 1
fi

echo "bootstrap: 克隆 $UPSTREAM"
git clone --quiet "$UPSTREAM" "$TCC_DIR"

echo "bootstrap: 检出基线 $BASE_COMMIT"
if ! git -C "$TCC_DIR" checkout --quiet --detach "$BASE_COMMIT" 2>/dev/null; then
    echo "bootstrap: 基线 $BASE_COMMIT 不在仓库中，尝试从 origin 获取" >&2
    git -C "$TCC_DIR" fetch --quiet origin "$BASE_COMMIT"
    git -C "$TCC_DIR" checkout --quiet --detach "$BASE_COMMIT"
fi

for p in "$ROOT"/patches/*.patch; do
    [ -e "$p" ] || continue
    echo "bootstrap: 应用 $(basename "$p")"
    if ! git -C "$TCC_DIR" -c user.name=c6 -c user.email=c6@localhost \
            am --quiet "$p"; then
        echo "bootstrap: 补丁应用失败：$(basename "$p")" >&2
        echo "bootstrap: 可用 'git -C tcc am --abort' 回滚后手工处理。" >&2
        exit 1
    fi
done

echo "bootstrap: 配置（./configure --prefix=tcc/dist）"
( cd "$TCC_DIR" && ./configure --prefix="$TCC_DIR/dist" >/dev/null )

cat <<EOF

bootstrap: 完成。下一步：

  make build        # 打开扩展构建（EXTRA-DEFS=-DC6_EXT）
  make selftest     # 跑 tests/ 下的 c6 自测
  make test         # 跑上游 tests2 全量测试
  ./tcc/tcc -B tcc -std=c6 -run hello.c   # 在仓库根目录运行

EOF
