/* superc 自测：#macro 与卫生的 __local 名字
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=tcc-ext -test tests/macro.c
 */
#include <stdio.h>
#include <string.h>

#macro swap(a, b) {
    typeof(a) __local tmp = (a);
    (a) = (b);
    (b) = tmp;
}

#macro maxv(x, y, out) {
    typeof(x) __local m = (x);
    if ((y) > m)
        m = (y);
    (out) = m;
}

#macro twice(stmt) {
    stmt;
    stmt;
}

#macro outer(a, b) {
    typeof(a) __local t = (a);
    swap(t, b);
    (a) = t;
}

static char logbuf[128] = {0};
static int loglen = 0;

static void lg_reset(void)
{
    loglen = 0;
    logbuf[0] = '\0';
}

static void lg(const char *s)
{
    while (*s && loglen < (int)sizeof(logbuf) - 2)
        logbuf[loglen++] = *s++;
    logbuf[loglen] = '\0';
}

#define LG_IS(s) (0 == strcmp(logbuf, (s)))

/* ---- 测试块 ---- */

test "#macro swap 交换两个变量" {
    int a = 3, b = 9;
    swap(a, b);
    if (a != 9 || b != 3)
        err(1, "swap 没有交换");
}

test "#macro 里的 __local 不捕获同名用户变量" {
    int a = 3, b = 9;
    int tmp = 42;
    swap(tmp, a);
    if (tmp != 3 || a != 42)
        err(1, "swap 的结果不对");
    if (tmp != 3)
        err(2, "用户变量 tmp 被宏捕获");
}

test "#macro 可以带多个参数并用 typeof 推导类型" {
    int a = 3, b = 9, m = 0;
    maxv(a, b, m);
    if (m != 9)
        err(1, "maxv 没有取到较大值");
    maxv(b, a, m);
    if (m != 9)
        err(2, "maxv 交换实参后结果不对");
}

test "#macro 可以展开语句两次" {
    lg_reset();
    twice(lg("x"));
    if (!LG_IS("xx"))
        err(1, "twice 没有展开两次");
}

test "#macro 可以调用另一个 #macro" {
    int a = 3, b = 9;
    outer(a, b);
    if (a != 9 || b != 3)
        err(1, "嵌套宏 outer 的结果不对");
}

test "同一个作用域里多次展开不冲突" {
    int a = 3, b = 9;
    swap(a, b);
    swap(a, b);
    if (a != 3 || b != 9)
        err(1, "两次 swap 之后应当还原");
    swap(a, b);
    swap(a, b);
    swap(a, b);
    if (a != 9 || b != 3)
        err(2, "五次 swap 之后应当交换");
}
