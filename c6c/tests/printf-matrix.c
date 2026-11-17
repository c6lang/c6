/* printf 格式化矩阵：既能被 cc（glibc）编，也能被 c6c 编。
 *
 * c6c/tests/printf-check.sh 把同一份源码用两个编译器各跑一遍，逐行 diff——
 * 不一样的每一行都是 c6c 运行时（c6c/lib/rt.c6 里的 c6c_vfmt）与 glibc 的口径
 * 差异。矩阵刻意只用 C 的公共子集，所以它既是测试也是"口径清单"。
 *
 * 覆盖：宽度/精度/标志（- 0 + 空格 #）、整数各进制与长度修饰（hd/hhu/ld/llu/zu）、
 * 字符/字符串（含 %*s 与 %.*s）、浮点 %f/%e/%g 的精度与舍入（正好 .5 时取偶）、
 * -0.0、1e19 以上整数部分的精确展开、snprintf 的截断与返回长度。
 */
#include <stdio.h>
#include <string.h>

static void line(const char *tag, const char *body)
{
    printf("%-14s|%s|\n", tag, body);
}

int main(void)
{
    char buf[256];
    int i = 0;
    int v = 42;
    int n = -42;
    int z = 0;
    unsigned u = 42u;
    long l = 1234567890123L;
    long nl = -1234567890123L;
    unsigned long ul = 1234567890123uL;

    /* 整数：宽度 / 标志 / 精度 */
    snprintf(buf, sizeof buf, "%d", v); line("%d", buf);
    snprintf(buf, sizeof buf, "%5d", v); line("%5d", buf);
    snprintf(buf, sizeof buf, "%-5d", v); line("%-5d", buf);
    snprintf(buf, sizeof buf, "%05d", v); line("%05d", buf);
    snprintf(buf, sizeof buf, "%+d", v); line("%+d", buf);
    snprintf(buf, sizeof buf, "% d", v); line("% d", buf);
    snprintf(buf, sizeof buf, "%.3d", v); line("%.3d", buf);
    snprintf(buf, sizeof buf, "%8.3d", v); line("%8.3d", buf);
    snprintf(buf, sizeof buf, "%d", n); line("%d-", buf);
    snprintf(buf, sizeof buf, "%+d", n); line("%+d-", buf);
    snprintf(buf, sizeof buf, "%05d", n); line("%05d-", buf);
    snprintf(buf, sizeof buf, "%d", z); line("%d=0", buf);
    snprintf(buf, sizeof buf, "%u", u); line("%u", buf);
    snprintf(buf, sizeof buf, "%o", u); line("%o", buf);
    snprintf(buf, sizeof buf, "%#o", u); line("%#o", buf);
    snprintf(buf, sizeof buf, "%x", u); line("%x", buf);
    snprintf(buf, sizeof buf, "%X", u); line("%X", buf);
    snprintf(buf, sizeof buf, "%#x", u); line("%#x", buf);
    snprintf(buf, sizeof buf, "%#X", u); line("%#X", buf);
    snprintf(buf, sizeof buf, "%08x", u); line("%08x", buf);
    snprintf(buf, sizeof buf, "%8.4x", u); line("%8.4x", buf);
    snprintf(buf, sizeof buf, "%.0d", 0); line("%.0d=0", buf);
    snprintf(buf, sizeof buf, "%x", 0); line("%x=0", buf);
    snprintf(buf, sizeof buf, "%#x", 0); line("%#x=0", buf);
    /* 长度修饰 */
    snprintf(buf, sizeof buf, "%ld", l); line("%ld", buf);
    snprintf(buf, sizeof buf, "%ld", nl); line("%ld-", buf);
    snprintf(buf, sizeof buf, "%lu", ul); line("%lu", buf);
    snprintf(buf, sizeof buf, "%lx", ul); line("%lx", buf);
    snprintf(buf, sizeof buf, "%llu", (unsigned long long)ul); line("%llu", buf);
    snprintf(buf, sizeof buf, "%lld", (long long)nl); line("%lld", buf);
    snprintf(buf, sizeof buf, "%zu", (unsigned long)sizeof buf); line("%zu", buf);
    snprintf(buf, sizeof buf, "%hd", (short)-5); line("%hd", buf);
    snprintf(buf, sizeof buf, "%hhu", (unsigned char)200); line("%hhu", buf);
    /* 字符与字符串 */
    snprintf(buf, sizeof buf, "%c", 'A'); line("%c", buf);
    snprintf(buf, sizeof buf, "%5c", 'A'); line("%5c", buf);
    snprintf(buf, sizeof buf, "%-5c", 'A'); line("%-5c", buf);
    snprintf(buf, sizeof buf, "%s", "abc"); line("%s", buf);
    snprintf(buf, sizeof buf, "%6s", "abc"); line("%6s", buf);
    snprintf(buf, sizeof buf, "%-6s", "abc"); line("%-6s", buf);
    snprintf(buf, sizeof buf, "%.2s", "abc"); line("%.2s", buf);
    snprintf(buf, sizeof buf, "%6.2s", "abc"); line("%6.2s", buf);
    snprintf(buf, sizeof buf, "%.*s", 2, "abc"); line("%.*s", buf);
    snprintf(buf, sizeof buf, "%*d", 6, 7); line("%*d", buf);
    snprintf(buf, sizeof buf, "%d%%", 5); line("%d%%", buf);
    /* 浮点 */
    snprintf(buf, sizeof buf, "%f", 3.14159265); line("%f", buf);
    snprintf(buf, sizeof buf, "%.2f", 3.14159265); line("%.2f", buf);
    snprintf(buf, sizeof buf, "%10.3f", -3.14159265); line("%10.3f", buf);
    snprintf(buf, sizeof buf, "%-10.3f", 3.14159265); line("%-10.3f", buf);
    snprintf(buf, sizeof buf, "%+.1f", 2.25); line("%+.1f", buf);
    snprintf(buf, sizeof buf, "%08.2f", 3.14159); line("%08.2f", buf);
    snprintf(buf, sizeof buf, "%e", 1234.5678); line("%e", buf);
    snprintf(buf, sizeof buf, "%.3e", 0.000123456); line("%.3e", buf);
    snprintf(buf, sizeof buf, "%E", 1234.5678); line("%E", buf);
    snprintf(buf, sizeof buf, "%g", 0.0001); line("%g", buf);
    snprintf(buf, sizeof buf, "%g", 100000.0); line("%g1e5", buf);
    snprintf(buf, sizeof buf, "%g", 1000000.0); line("%g1e6", buf);
    snprintf(buf, sizeof buf, "%g", 1234567.0); line("%g", buf);
    snprintf(buf, sizeof buf, "%.3g", 3.14159265); line("%.3g", buf);
    snprintf(buf, sizeof buf, "%G", 1e-10); line("%G", buf);
    snprintf(buf, sizeof buf, "%f", 0.0); line("%f=0", buf);
    snprintf(buf, sizeof buf, "%f", 0.0 * -1.0); line("%f-0", buf);
    snprintf(buf, sizeof buf, "%f", 1e20); line("%f1e20", buf);
    snprintf(buf, sizeof buf, "%f", 1.25e19); line("%f1.25e19", buf);
    snprintf(buf, sizeof buf, "%.0f", 2.5); line("%.0f", buf);
    snprintf(buf, sizeof buf, "%5.1f%%", 12.34); line("%5.1f%%", buf);
    /* 返回值与越界 */
    i = snprintf(buf, sizeof buf, "abc%dx", 7);
    printf("ret=%d\n", i);
    i = snprintf(buf, 4, "abcdefgh");
    printf("trunc=%d [%s]\n", i, buf);
    buf[0] = 'x'; buf[1] = 0;
    i = snprintf(buf, 0, "abcdef");
    printf("zero=%d\n", i);
    return 0;
}
