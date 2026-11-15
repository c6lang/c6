#!/bin/sh
# -b（数组下标边界检查）的回归。
#
# 口径：
#   * 越界**写**和越界**读**都要被拦住：stderr 一行点名 + 退出码 2，
#     越界语句之后的输出不能出现（说明真的停在那儿了）；
#   * 界内访问一个字都不能动：同一个程序不加 -b 与加 -b 的输出要一致；
#   * 不加 -b 时不检查（默认行为不变）——用同一个越界程序对照；
#   * 检查跟着**数组类型**走：结构体里的数组字段、多维数组、二维的行都要查；
#   * 只查编译期知道长度的数组：指针下标与 VLA 不插检查（如实记在文档里，
#     这里用"不会误报"来验）。
set -u

C6C=${C6C:-./build/c6c}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
TMPDIR="$WORK/tmp"
export TMPDIR
mkdir -p "$TMPDIR"

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "ok   $1"; }
bad() { fail=$((fail + 1)); echo "FAIL $1${2:+：$2}"; }

case "$C6C" in
/*) ;;
*) C6C="$(pwd)/$C6C" ;;
esac

# --- 1. 越界写被拦住 ---
cat >"$WORK/woob.c6" <<'EOF'
int printf(const char *fmt, ...);

int main(void)
{
    int a[3] = {0};
    int j = 5;

    printf("before\n");
    a[j] = 22;
    printf("BAD 越界没被拦住\n");
    return 0;
}
EOF
if "$C6C" -b -o "$WORK/woob" "$WORK/woob.c6" >"$WORK/c1.log" 2>&1; then
    out=$("$WORK/woob" 2>"$WORK/woob.err")
    rc=$?
    if [ "$rc" = 2 ] && [ "$out" = "before" ]; then
        ok "越界写被拦住：停在越界语句之前，退出码 2"
    else
        bad "越界写被拦住" "退出码 $rc，stdout『$out』"
    fi
    if grep -q "下标越界：index 5 不在 \[0, 3)" "$WORK/woob.err"; then
        ok "运行期消息点名了下标与边界（index 5 / [0, 3)）"
    else
        bad "越界消息" "$(cat "$WORK/woob.err")"
    fi
else
    bad "带 -b 编译" "$(tail -3 "$WORK/c1.log")"
fi

# --- 2. 越界读也被拦住 ---
cat >"$WORK/roob.c6" <<'EOF'
int printf(const char *fmt, ...);

int main(void)
{
    int a[2] = {7, 8};
    int j = 4;
    int v = a[j];

    printf("BAD 越界读没被拦住 v=%d\n", v);
    return 0;
}
EOF
if "$C6C" -b -o "$WORK/roob" "$WORK/roob.c6" >"$WORK/c2.log" 2>&1; then
    out=$("$WORK/roob" 2>"$WORK/roob.err")
    rc=$?
    if [ "$rc" = 2 ] && [ -z "$out" ]; then
        ok "越界读被拦住（没有输出、退出码 2）"
    else
        bad "越界读被拦住" "退出码 $rc，stdout『$out』"
    fi
else
    bad "越界读用例编译" "$(tail -3 "$WORK/c2.log")"
fi

# --- 3. 界内访问不受影响：加不加 -b 输出一致 ---
cat >"$WORK/inb.c6" <<'EOF'
int printf(const char *fmt, ...);

struct S { int a[4]; };

int main(void)
{
    int a[5] = {1, 2, 3, 4, 5};
    int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
    struct S s = {{10, 20, 30, 40}};
    int i = 0;
    int sum = 0;

    for (i = 0; i < 5; i++)
        sum += a[i];
    sum += m[1][2] + s.a[3] + a[0];
    printf("sum=%d\n", sum);
    return 0;
}
EOF
"$C6C" -o "$WORK/inb0" "$WORK/inb.c6" >/dev/null 2>&1
"$C6C" -b -o "$WORK/inb1" "$WORK/inb.c6" >/dev/null 2>&1
o0=$("$WORK/inb0" 2>&1)
o1=$("$WORK/inb1" 2>&1)
if [ "$o0" = "sum=62" ] && [ "$o1" = "$o0" ]; then
    ok "界内访问：一维/二维/结构体数组字段都对，加 -b 输出不变（sum=62）"
else
    bad "界内访问" "不加 -b『$o0』，加 -b『$o1』（应为 sum=62）"
fi

# --- 4. 不加 -b 时不检查（默认行为不变） ---
if [ -x "$WORK/woob" ]; then
    "$C6C" -o "$WORK/woob0" "$WORK/woob.c6" >/dev/null 2>&1
    out=$("$WORK/woob0" 2>&1)
    rc=$?
    if [ "$rc" = 0 ] && [ "$out" = "before
BAD 越界没被拦住" ]; then
        ok "不加 -b 时照旧不检查（默认行为没变）"
    else
        bad "不加 -b 的行为" "退出码 $rc，输出『$out』"
    fi
fi

# --- 5. 二维数组的第二维也查（m[2][0]） ---
cat >"$WORK/m2.c6" <<'EOF'
int printf(const char *fmt, ...);

int main(void)
{
    int m[2][3] = {{0}};
    int i = 2;

    m[i][0] = 1;
    printf("BAD 第二维越界没被拦住\n");
    return 0;
}
EOF
if "$C6C" -b -o "$WORK/m2" "$WORK/m2.c6" >/dev/null 2>&1; then
    out=$("$WORK/m2" 2>"$WORK/m2.err")
    rc=$?
    if [ "$rc" = 2 ] && [ -z "$out" ]; then
        ok "二维数组行越界被拦住（m[2][0]）"
    else
        bad "二维数组行越界" "退出码 $rc，输出『$out』"
    fi
else
    bad "二维越界用例编译" "编译失败"
fi

# --- 6. 结构体里的数组字段越界也查（s.a[4]） ---
cat >"$WORK/sf.c6" <<'EOF'
int printf(const char *fmt, ...);

struct S { int a[4]; };

int main(void)
{
    struct S s = {{0}};
    int i = 4;

    s.a[i] = 9;
    printf("BAD 结构体数组字段越界没被拦住\n");
    return 0;
}
EOF
if "$C6C" -b -o "$WORK/sf" "$WORK/sf.c6" >/dev/null 2>&1; then
    out=$("$WORK/sf" 2>"$WORK/sf.err")
    rc=$?
    if [ "$rc" = 2 ] && [ -z "$out" ]; then
        ok "结构体数组字段越界被拦住（s.a[4]）"
    else
        bad "结构体数组字段越界" "退出码 $rc，输出『$out』"
    fi
else
    bad "结构体数组字段用例编译" "编译失败"
fi

# --- 7. 只查编译期知道长度的数组：指针下标与 VLA 不插检查 ---
cat >"$WORK/ptr.c6" <<'EOF'
int printf(const char *fmt, ...);

int main(void)
{
    int a[4] = {1, 2, 3, 4};
    int *p = a;
    int n = 4;
    int vla[n];
    int i = 0;
    int sum = 0;

    for (i = 0; i < n; i++)
        vla[i] = i * 2;
    for (i = 0; i < n; i++)
        sum += vla[i];
    sum += p[3];
    printf("sum=%d\n", sum);
    return 0;
}
EOF
if "$C6C" -b -o "$WORK/ptr" "$WORK/ptr.c6" >"$WORK/c7.log" 2>&1; then
    out=$("$WORK/ptr" 2>"$WORK/ptr.err")
    rc=$?
    if [ "$rc" = 0 ] && [ "$out" = "sum=16" ]; then
        ok "指针下标与 VLA 不插检查（不会误报，sum=16）"
    else
        bad "指针下标/VLA 不误报" "退出码 $rc，输出『$out』"
    fi
else
    bad "指针下标用例编译" "$(tail -3 "$WORK/c7.log")"
fi

# --- 8. -run 模式也照查 ---
out=$("$C6C" -b -run "$WORK/woob.c6" 2>"$WORK/run.err")
rc=$?
if [ "$rc" = 2 ] && [ "$out" = "before" ]; then
    ok "-run -b 同样拦住越界（退出码 2）"
else
    bad "-run -b" "退出码 $rc，输出『$out』"
fi

echo "c6c -b 边界检查回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
