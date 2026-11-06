#!/bin/sh
# 生成/同步 c6 的 TCC 工作树：
#   1. 首次：克隆上游 TinyCC，检出固定基线（BASE_COMMIT）；
#   2. 已有：把工作树同步到 patches/ 的当前内容——补丁增删、改名或内容
#      修改后，回到基线重新应用全部补丁（tcc/ 是生成物，不手工编辑）；
#   3. 依次应用 patches/*.patch；
#   4. 运行 ./configure 生成 config.mak。
#
# 是否需要同步由 tcc/.c6-stamp 判断：它记录基线提交和每个补丁文件的内容
# 哈希。所以改了补丁（哪怕文件名和数量不变）也会触发重建。
#
# 可用环境变量：
#   C6_TCC_URL   上游仓库地址（默认 GitHub；离线时可指向本地克隆）
#   C6_TCC_BASE  基线提交（默认见下）
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TCC_DIR="$ROOT/tcc"
BASE_COMMIT=${C6_TCC_BASE:-2d3eea2}
UPSTREAM=${C6_TCC_URL:-https://github.com/TinyCC/tinycc.git}
STAMP="$TCC_DIR/.c6-stamp"

# patches/*.patch 的清单（没有补丁时什么都不输出）
patch_files() {
    for p in "$ROOT"/patches/*.patch; do
        [ -e "$p" ] || continue
        printf '%s\n' "$p"
    done
}

patch_count() {
    patch_files | wc -l | tr -d ' '
}

# 工作树指纹：基线 + 每个补丁的文件名与内容哈希。
worktree_stamp() {
    printf 'base %s\n' "$BASE_COMMIT"
    patch_files | while IFS= read -r p; do
        printf '%s %s\n' "$(basename "$p")" \
            "$(git -C "$ROOT" hash-object -- "$p")"
    done
}

apply_patches() {
    for p in "$ROOT"/patches/*.patch; do
        [ -e "$p" ] || continue
        echo "bootstrap: 应用 $(basename "$p")"
        if ! git -C "$TCC_DIR" -c user.name=c6 -c user.email=c6@localhost \
                am --quiet "$p"; then
            echo "bootstrap: 补丁应用失败：$(basename "$p")" >&2
            echo "bootstrap: 已回滚到基线；修正补丁后重试。" >&2
            git -C "$TCC_DIR" am --abort >/dev/null 2>&1 || true
            return 1
        fi
    done
}

configure_tcc() {
    echo "bootstrap: 配置（./configure --prefix=tcc/dist）"
    ( cd "$TCC_DIR" && ./configure --prefix="$TCC_DIR/dist" >/dev/null )
}

if [ ! -e "$TCC_DIR" ]; then
    echo "bootstrap: 克隆 $UPSTREAM"
    if ! git clone --quiet "$UPSTREAM" "$TCC_DIR"; then
        echo "bootstrap: 克隆失败；离线时可用 C6_TCC_URL 指向本地 tinycc 克隆。" >&2
        exit 1
    fi
elif [ ! -d "$TCC_DIR/.git" ]; then
    echo "bootstrap: $TCC_DIR 存在但不是 git 工作树，请先删除或改名。" >&2
    exit 1
fi

# 工作树已经对应当前补丁集：什么都不用做
want=$(worktree_stamp)
if [ -f "$STAMP" ] && [ -f "$TCC_DIR/config.mak" ] \
        && [ "$(cat "$STAMP")" = "$want" ]; then
    echo "bootstrap: $TCC_DIR 已是最新（$(patch_count) 个补丁）"
    exit 0
fi

# 上次应用补丁中途失败时先把半套改动回滚掉
if [ -d "$TCC_DIR/.git/rebase-apply" ]; then
    echo "bootstrap: 回滚上次中断的补丁应用"
    git -C "$TCC_DIR" am --abort >/dev/null 2>&1 || true
fi

# tcc/ 是生成物；有手改的跟踪文件就停下来，别静默覆盖
if [ -n "$(git -C "$TCC_DIR" status --porcelain --untracked-files=no)" ]; then
    echo "bootstrap: $TCC_DIR 有未提交的修改，拒绝覆盖：" >&2
    git -C "$TCC_DIR" status --short --untracked-files=no >&2
    echo "bootstrap: 把改动做成 patches/ 里的补丁，或执行 'make distclean' 丢弃。" >&2
    exit 1
fi

# 基线提交必须在本地仓库里；首次克隆后一般就有
if ! git -C "$TCC_DIR" rev-parse --verify --quiet "$BASE_COMMIT^{commit}" >/dev/null; then
    echo "bootstrap: 基线 $BASE_COMMIT 不在仓库中，尝试从 origin 获取" >&2
    git -C "$TCC_DIR" fetch --quiet origin "$BASE_COMMIT"
fi

echo "bootstrap: 同步 $TCC_DIR 到当前 $(patch_count) 个补丁（基线 $BASE_COMMIT）"
git -C "$TCC_DIR" reset --hard --quiet "$BASE_COMMIT"
git -C "$TCC_DIR" clean -fdxq
apply_patches
configure_tcc
printf '%s\n' "$want" > "$STAMP"

cat <<EOF

bootstrap: 完成。下一步：

  make build        # 打开扩展构建（EXTRA-DEFS=-DC6_EXT）
  make selftest     # 跑 tests/ 下的 c6 自测
  make test         # 跑上游 tests2 全量测试
  ./tcc/tcc -B tcc -std=c6 -run hello.c   # 在仓库根目录运行

EOF
