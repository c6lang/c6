#!/bin/sh
# 二进制转义回归：`\xNN` / `\NNN` 是**字节**，`\uNNNN` 才是**码点**。
#
# 这条线踩过：窄字面量里凡是从转义拿到的值 >= 0x80 都被当成码点重新 UTF-8
# 编码，于是 `"\xde"` 变成 0xc3 0x9e（两个字节）——std/encoding/hexdump 这类
# 拿字符串拼二进制数据的地方全被带偏，一连 5 个用例挂在这上面。
#
# 断言：
#   * `"\xde\xad"` 就是 de ad 两个字节，长度 3（含 NUL）；
#   * 八进制 `"\336\255"` 同上（同一个值，两种写法）；
#   * `'\xde'` 是 -34（char 有符号）；
#   * `"\u00e9"` 是 UTF-8 的 c3 a9（码点，该编码就编码）；
#   * `"\u4e2d"` 是 e4 b8 ad；
#   * `L"\u4e2d"` 的码点真是 0x4e2d（宽字面量不能丢码点）；
#   * `"\x00ab"` 里的 0 是内容，不是结尾（sizeof 说了算）；
#   * 相邻字面量拼接跨过转义边界也对：`"\xde" "ad"`。
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

cat >"$WORK/esc.c6" <<'EOF'
#include <stdio.h>
#include <string.h>

static int bad;

static void ck(int cond, const char *what)
{
    if (cond)
        printf("ok   %s\n", what);
    else {
        printf("FAIL %s\n", what);
        bad++;
    }
}

static int bytes_are(const char *p, int n, const unsigned char *want)
{
    int i = 0;

    for (i = 0; i < n; i++)
        if ((unsigned char)p[i] != want[i])
            return 0;
    return 1;
}

int main(void)
{
    const unsigned char de_ad[2] = { 0xde, 0xad };
    const unsigned char e_acute[2] = { 0xc3, 0xa9 };
    const unsigned char zhong[3] = { 0xe4, 0xb8, 0xad };
    const char *s = "\xde\xad";

    ck(sizeof "\xde\xad" == 3, "\\xNN 占一个字节（sizeof=3）");
    ck(bytes_are(s, 2, de_ad), "\\xde\\xad 的字节就是 de ad");
    ck(sizeof "\336\255" == 3, "八进制 \\NNN 也是一个字节");
    ck(bytes_are("\336\255", 2, de_ad), "\\336\\255 与 \\xde\\xad 等价");
    ck('\xde' == -34, "'\\xde' 是 -34（char 有符号）");
    ck(sizeof('\xde') == 4, "字符常量的类型是 int");
    ck(sizeof "\u00e9" == 3, "\\u 是码点，窄字面量按 UTF-8 编码（两字节）");
    ck(bytes_are("\u00e9", 2, e_acute), "\\u00e9 的字节是 c3 a9");
    ck(bytes_are("\u4e2d", 3, zhong), "\\u4e2d 的字节是 e4 b8 ad");
    ck(sizeof(L"\u4e2d") == 8, "L\"\\u4e2d\" 是 4 字节元素 + NUL");
    ck((int)L"\u4e2d"[0] == 0x4e2d, "宽字面量拿到的码点是 0x4e2d");
    /* `\x` 是**贪心**的：`"\x00ab"` 是一个 0xab 字节（gcc 会 warning，值一样），
     * 想表达 00 61 62 得断成两段字面量。 */
    ck(sizeof "\x00" "ab" == 4, "\\x00 是内容，不截断字符串");
    ck(sizeof "\x00ab" == 2 && (unsigned char)"\x00ab"[0] == 0xab,
       "\\x 贪心：\\x00ab 是一个字节 0xab");
    ck(sizeof "\xde" "ad" == 4, "拼接跨转义边界（de 61 64 + NUL）");
    ck(bytes_are("\xde" "ad", 3, (const unsigned char *)"\xde\x61\x64"),
       "拼接后的字节是 de 61 64");
    return bad != 0;
}
EOF

if ! "$C6C" -o "$WORK/esc" "$WORK/esc.c6" >"$WORK/c.log" 2>&1; then
    bad "编译 esc.c6" "$(grep -v 'warning: type defaults' "$WORK/c.log" | tail -3)"
    echo "c6c 二进制转义回归：通过 $pass / 失败 $fail"
    exit 1
fi
ok "编译 esc.c6"

# 断言失败时程序自己返回非 0，这里只看它有没有真的跑起来（输出为空才算挂）
"$WORK/esc" >"$WORK/run.txt" 2>"$WORK/run.err"
if [ ! -s "$WORK/run.txt" ]; then
    bad "运行 esc" "$(tail -3 "$WORK/run.err")"
fi
# 程序自己打印 ok/FAIL，这里只汇总
p=$(grep -c '^ok ' "$WORK/run.txt")
f=$(grep -c '^FAIL ' "$WORK/run.txt")
[ "$p" -gt 0 ] && pass=$((pass + p))
[ "$f" -gt 0 ] && fail=$((fail + f))
sed -n 's/^\(ok\|FAIL\)/&/p' "$WORK/run.txt"

echo "c6c 二进制转义回归：通过 $pass / 失败 $fail"
[ "$fail" -eq 0 ]
