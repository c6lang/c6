#!/bin/sh
# 嵌套模板实例化：A@T 的方法里用到 B@T 时，B 的方法体必须真发出来。
#
# 踩过：模板体带方法时先跑一遍"只登记签名"，A 的方法签名里出现 B@T 就在那一遍
# 里把 B 实例化了（只有签名）；外层第二遍重放到同样的实参时，`tmpl_inst` 的
# 缓存直接短路返回，B 的方法体再也没机会展开——链接期报 `<实例名>_<方法>`
# 未定义。std 的 `Vect@T.iter()` 返回 `Iter@T` 正是这个形状（`Iter_int_has`）。
#
# 断言：
#   1. 两个 TU（模块 + 主程序）编得过、链得上（不再有未定义符号）；
#   2. 跑起来结果正确（迭代器真的走了三个元素）；
#   3. 编译日志里没有"未定义的符号"（这正是这条线曾经的症状）；
#   4. 模块单独 `-c` 也编得过（实例化发生在别的 TU 时不会把模块带崩）。
set -u

C6C=${C6C:-./build/c6c}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
TMPDIR="$WORK/tmp"
export TMPDIR
mkdir -p "$TMPDIR" "$WORK/rp"

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }


case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac

cat >"$WORK/rp/tmpl.c6" <<'EOF'
module rp.tmpl;

struct Box@T {
    T *data;
    int n;

    It@T iter()
    {
        It@T it = { self, 0 };

        return it;
    }
};

struct It@T {
    Box@T *box;
    int i;

    int has() { return i < box->n; }

    T next()
    {
        T x = box->data[i];

        i++;
        return x;
    }
};
EOF

cat >"$WORK/main.c6" <<'EOF'
import rp.tmpl;

int main(void)
{
    int a[3] = { 1, 2, 3 };
    Box@int b = { a, 3 };
    It@int it = b.iter();
    int n = 0;

    while (it.has())
        n = n + it.next();
    return n == 6 ? 0 : 1;
}
EOF

if "$C6C" -I "$WORK" -o "$WORK/prog" "$WORK/main.c6" "$WORK/rp/tmpl.c6" >"$WORK/c.log" 2>&1; then
    ok "编译链接（嵌套实例的方法体有着落）"
else
    bad "编译链接" "$(grep -v 'warning: type defaults' "$WORK/c.log" | tail -3)"
    echo "c6c 嵌套模板回归：通过 $pass / 失败 $fail"
    exit 1
fi

if grep -q '未定义的符号' "$WORK/c.log"; then
    bad "没有未定义符号" "$(grep '未定义的符号' "$WORK/c.log" | head -2)"
else
    ok "没有未定义符号"
fi

if "$WORK/prog"; then
    ok "运行结果正确（迭代器走了三个元素）"
else
    bad "运行结果" "退出码 $?"
fi

# 模块单独编一遍（-c）：实例方法在 ET_REL 里也要有定义（不是 U）
if "$C6C" -I "$WORK" -c "$WORK/rp/tmpl.c6" -o "$WORK/tmpl.o" >"$WORK/c2.log" 2>&1; then
    ok "-c 单编模块"
else
    bad "-c 单编模块" "$(grep -v 'warning: type defaults' "$WORK/c2.log" | tail -2)"
fi

echo "c6c 嵌套模板回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
