/* superc 自测：defer 的执行时机、顺序与作用域
 *
 * 运行（仓库根目录）：
 *   make selftest
 * 或单个文件：
 *   ./tcc/tcc -B tcc -std=tcc-ext -test tests/defer.c
 */
#include <stdio.h>
#include <string.h>

/* ---- 清理顺序记录 ---- */
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

/* ---- 被测函数 ---- */

/* 同一个块里注册多个 defer：退出时逆序执行 */
static void basic(void)
{
    lg_reset();
    {
        defer lg("a");
        defer lg("b");
        defer lg("c");
        lg("body");
    }
}

/* break/continue 离开循环体时，循环体里的 defer 也会执行 */
static void in_loop(void)
{
    int i = 0;
    lg_reset();
    for (i = 0; i < 3; i++) {
        defer lg("d");
        if (i == 1)
            continue;
        lg("i");
        if (i == 2)
            break;
    }
}

/* return 离开函数时执行，且不影响返回值 */
static int ret(int x)
{
    lg_reset();
    defer lg("one");
    if (x > 0) {
        defer lg("two");
        return x * 2;
    }
    defer lg("three");
    return -1;
}

/* goto 跳出一个块时，只执行正在离开的块里的 defer */
static void goto_out(void)
{
    lg_reset();
    {
        defer lg("outer");
        {
            defer lg("inner");
            goto out;
        }
    }
out:
    ;
}

/* 递归：每一帧的 defer 各自独立 */
static int rec(int n)
{
    defer lg("r");
    if (n <= 0)
        return 0;
    return n + rec(n - 1);
}

/* ---- 测试块 ---- */

test "defer 在块退出时按注册的逆序执行" {
    basic();
    if (!LG_IS("bodycba"))
        err(1, "defer 没有按逆序执行");
}

test "break/continue 也会执行循环体的 defer" {
    in_loop();
    if (!LG_IS("iddid"))
        err(1, "break/continue 的 defer 顺序不对");
}

test "return 执行 defer 并保留返回值" {
    int v = 0;
    v = ret(5);
    if (v != 10)
        err(1, "带 defer 的 return 返回值不对");
    if (!LG_IS("twoone"))
        err(2, "return 路径的 defer 顺序不对");
    v = ret(-5);
    if (v != -1)
        err(3, "错误分支的返回值不对");
    if (!LG_IS("threeone"))
        err(4, "错误分支的 defer 顺序不对");
}

test "goto 只执行被离开块的 defer" {
    goto_out();
    if (!LG_IS("innerouter"))
        err(1, "goto 的 defer 顺序不对");
}

test "递归时每帧的 defer 独立执行" {
    lg_reset();
    if (rec(3) != 6)
        err(1, "递归返回值不对");
    if (!LG_IS("rrrr"))
        err(2, "递归帧的 defer 数量不对");
}
