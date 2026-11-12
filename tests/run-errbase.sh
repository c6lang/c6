#!/bin/sh
# c6 错误码号段表（errbase.def）的检查：`make errcheck` 调用。
#
# 这个脚本守两件事：
#   1. 仓库根目录的 errbase.def 与「现在 --emit-errbase 会产出的东西」一致：
#      有人加了模块却忘了重新钉号，或者手改表改了别人的号，都在这里报出来。
#   2. 表本身的各种约束（C126/C127/C128）真的会报错，而不是被静默接受。
#
# 环境变量 TCC 覆盖编译器路径（默认 <仓库>/tcc/c6）。
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TCC=${TCC:-$ROOT/tcc/c6}
BASE="-B $ROOT/tcc -I $ROOT"
# 自动创建那一节不能用 -I $ROOT：仓库自己的表就在那里，会被优先找到
BASE_MIN="-B $ROOT/tcc"

TMP=${TMPDIR:-/tmp}/c6-errbase.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

npass=0
nfail=0

pass() { npass=$((npass + 1)); echo "ok   $1"; }
fail() { nfail=$((nfail + 1)); echo "FAIL: $1" >&2; shift; [ $# -eq 0 ] || printf '%s\n' "$@" | sed 's/^/      /' >&2; }

if [ ! -x "$TCC" ]; then
    echo "run-errbase: 找不到 $TCC，先执行 'make build'" >&2
    exit 1
fi

# 仓库里所有模块文件（模块名 = 文件路径去掉 .c6）
# 跳过 .git：会话工作树（.git/dsh-worktrees/*）里是同一棵树的完整副本，扫进去
# 每个模块都会出现两次，钉号时报 'link symbol ... defined twice'
modules() {
    cd "$ROOT" && grep -rl '^module ' --include=*.c6 --exclude-dir=.git . \
        | sed 's,^\./,,' | sort
}

# 定义 main 的模块文件必须从 '-emit-errbase' 的清单里剔掉：那个选项走的是
# 正常编译流程，末尾会把这些文件连成一个可执行文件，同一份清单里有两个 main
# 就报 "link symbol 'main' defined twice"，整项检查跑不完。这些文件本来也
# 不需要钉号（main 所在的 TU 不属于任何模块的号段），剔掉不影响表的内容。
# 排除表是显式的：扫出来的带 main 文件集合必须与它完全一致，新增了带 main 的
# 模块文件就在这里失败，逼人确认之后才放行，而不是被静默过滤掉。
MAIN_MODULES='tests/errors/init.c6 tests/errors/module.c6'
main_bearing() {
    cd "$ROOT" && grep -lE '^[[:space:]]*(int|void)[[:space:]]+main[[:space:]]*\(' \
        "$@" 2>/dev/null | sed 's,^\./,,' | sort
}

# ---- 1. 提交进版本库的表必须是最新的 ----
all_mods=$(modules)
# 排除表必须与实际情况一致：多一个少一个都失败，不静默过滤。
# 两边都归一成「空格分隔、按名字排序」的字符串再比，免得一个换行分隔一个空格
# 分隔就永远不相等。
found_main=$(main_bearing $all_mods | tr '\n' ' ' | sed 's/ *$//')
table_main=$(printf '%s\n' $MAIN_MODULES | sort | tr '\n' ' ' | sed 's/ *$//')
if [ "$found_main" != "$table_main" ]; then
    fail "带 main 的模块文件和排除表对不上（MAIN_MODULES）" \
         "排除表: $table_main" \
         "实际扫到: $found_main"
fi
mods=$(for m in $all_mods; do
    keep=yes
    for x in $MAIN_MODULES; do
        [ "$m" = "$x" ] && keep=no && break
    done
    [ "$keep" = yes ] && echo "$m"
done)
inc=
for d in $(echo "$mods" | sed 's,/[^/]*$,,' | sort -u); do
    inc="$inc -I $ROOT/$d"
done
# shellcheck disable=SC2086
if $TCC $BASE $inc -emit-errbase="$TMP/fresh.def" $mods >"$TMP/err" 2>&1; then
    if diff -u "$ROOT/errbase.def" "$TMP/fresh.def" >"$TMP/diff" 2>&1; then
        pass "errbase.def 是最新的（$(grep -c '0x6' "$TMP/fresh.def") 个模块已钉号）"
    else
        fail "errbase.def 过期了：跑 './tcc/c6 -B tcc -I . -emit-errbase' 重新生成" \
             "$(cat "$TMP/diff")"
    fi
else
    fail "重新生成 errbase.def 失败" "$(cat "$TMP/err")"
fi

# ---- 2. 表里的错误要报出来 ----

# 一个最小工程：一个模块 + 一个 main
mkdir -p "$TMP/proj/app"
cat > "$TMP/proj/app/a.c6" <<'EOF'
module app.a;

enum Error {
    BAD = 1,
};
EOF
cat > "$TMP/proj/main.c6" <<'EOF'
import app.a;

int main(void)
{
    return (int)app.a.BAD == 0;
}
EOF

# expect_diag <名字> <期望的诊断片段> <编译参数>...
expect_diag() {
    _name=$1
    _want=$2
    shift 2
    if $TCC $BASE -I "$TMP/proj" "$@" -run "$TMP/proj/main.c6" \
            >"$TMP/out" 2>"$TMP/err"; then
        fail "$_name" "没有报错"
    elif grep -qF -- "$_want" "$TMP/err"; then
        pass "$_name"
    else
        fail "$_name" "缺少诊断: $_want" "$(cat "$TMP/err")"
    fi
}

# expect_ok <名字> <编译参数>...
expect_ok() {
    _name=$1
    shift
    if $TCC $BASE -I "$TMP/proj" "$@" -run "$TMP/proj/main.c6" \
            >"$TMP/out" 2>"$TMP/err"; then
        pass "$_name"
    else
        fail "$_name" "$(cat "$TMP/err")"
    fi
}

printf 'app.a 0x6100\n'        > "$TMP/good.def"
printf 'std.core.err 0x6100\n'      > "$TMP/std.def"
printf 'app.a 0x1000\n'        > "$TMP/low.def"
printf 'app.a 0x6100\napp.b 0x6100\n' > "$TMP/dup.def"
printf 'app.a 0x6100\napp.a 0x6101\n' > "$TMP/twice.def"
printf 'app.a\n'               > "$TMP/noslot.def"

expect_ok   "-errbase-file 指向的表会被读"    -errbase-file="$TMP/good.def"
expect_ok   "-no-errbase 不读表"              -no-errbase
expect_diag "-errbase-file 指定的表必须存在（C128）" "cannot read error block table" \
            -errbase-file="$TMP/missing.def"
expect_diag "std 模块的号段不能写进表里（C126）" \
            "the blocks of the std modules are built into the compiler" \
            -errbase-file="$TMP/std.def"
expect_diag "钉号必须落在 0x6100..0x7FFF（C126）" \
            "is outside the pinned pool" -errbase-file="$TMP/low.def"
expect_diag "同一个号不能给两个模块（C126）" \
            "is used by" -errbase-file="$TMP/dup.def"
expect_diag "同一个模块不能钉两个号（C126）" \
            "is pinned to" -errbase-file="$TMP/twice.def"
expect_diag "每一行都要有号（C126）" \
            "needs a slot" -errbase-file="$TMP/noslot.def"

# 自动池撞号：app.m150 与 app.m952 的 fnv1a32 落在同一个号段（C127）
mkdir -p "$TMP/coll/proj/app"
for m in m150 m952; do
    cat > "$TMP/coll/proj/app/$m.c6" <<EOF
module app.$m;

enum Error {
    X = 1,
};
EOF
done
cat > "$TMP/coll/proj/main.c6" <<'EOF'
import app.m150;
import app.m952;

int main(void)
{
    return 0;
}
EOF
if $TCC $BASE -I "$TMP/coll/proj" -no-errbase -run "$TMP/coll/proj/main.c6" \
        >"$TMP/out" 2>"$TMP/err"; then
    fail "两个模块撞进同一个号段要报错（C127）" "没有报错"
elif grep -qF "share the block" "$TMP/err"; then
    pass "两个模块撞进同一个号段要报错（C127）"
else
    fail "两个模块撞进同一个号段要报错（C127）" "$(cat "$TMP/err")"
fi

# ---- 3. 表的自动创建与 -emit-errbase 的稳定性 ----
mkdir -p "$TMP/auto"
cp -r "$TMP/proj/app" "$TMP/auto/"
cp "$TMP/proj/main.c6" "$TMP/auto/"
( cd "$TMP/auto" && $TCC $BASE_MIN -I . -o main.exe main.c6 >"$TMP/out" 2>"$TMP/err" )
if [ -f "$TMP/auto/errbase.def" ]; then
    if grep -q '^#' "$TMP/auto/errbase.def" \
       && ! grep -q '^[a-zA-Z]' "$TMP/auto/errbase.def"; then
        pass "没有表时自动创建一份只有注释的空表"
    else
        fail "自动创建的表应该只有注释" "$(cat "$TMP/auto/errbase.def")"
    fi
else
    fail "没有表时应该自动创建 errbase.def" "$(cat "$TMP/err")"
fi

rm -f "$TMP/auto/errbase.def"
( cd "$TMP/auto" && $TCC $BASE_MIN -I . -no-errbase -o main.exe main.c6 >"$TMP/out" 2>"$TMP/err" )
if [ -f "$TMP/auto/errbase.def" ]; then
    fail "-no-errbase 不应该写表"
else
    pass "-no-errbase 不写表"
fi

( cd "$TMP/auto" && $TCC $BASE_MIN -I . -emit-errbase main.c6 >"$TMP/out" 2>"$TMP/err" )
if [ -f "$TMP/auto/errbase.def" ] && grep -q '^app\.a  *0x6100$' "$TMP/auto/errbase.def"; then
    pass "-emit-errbase 把 app.a 钉在 0x6100"
else
    fail "-emit-errbase 没有钉住 app.a" "$(cat "$TMP/err")" \
         "$(cat "$TMP/auto/errbase.def" 2>/dev/null)"
fi

cp "$TMP/auto/errbase.def" "$TMP/first.def"
# 再跑一次：已有的行原样保留，输出必须一模一样
( cd "$TMP/auto" && $TCC $BASE_MIN -I . -emit-errbase main.c6 >"$TMP/out" 2>"$TMP/err" )
if diff -q "$TMP/first.def" "$TMP/auto/errbase.def" >/dev/null 2>&1; then
    pass "-emit-errbase 是幂等的（表不会抖动）"
else
    fail "-emit-errbase 第二次跑变了" \
         "$(diff -u "$TMP/first.def" "$TMP/auto/errbase.def")"
fi

# 钉住之后再编一次：id 必须就是表里的号
( cd "$TMP/auto" && $TCC $BASE_MIN -I . -o main.exe main.c6 >"$TMP/out" 2>"$TMP/err" )
if [ $? -eq 0 ] && [ -x "$TMP/auto/main.exe" ]; then
    pass "钉住之后还能正常编译"
else
    fail "钉住之后编译失败" "$(cat "$TMP/err")"
fi

echo
if [ $nfail -eq 0 ]; then
    echo "run-errbase: $npass 通过, 0 失败"
    exit 0
fi
echo "run-errbase: $npass 通过, $nfail 失败" >&2
exit 1
