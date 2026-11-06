/* c6 自测：元组 + async / await
 *
 * 'async (int, int) f(...)' 编译成一个返回 '(int, int)' 的协程，任务结构
 * 里的 'val' 字段就是那个元组；'await' 出来的值可以直接解构。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/tuple_async.c
 */
#include <stdio.h>

async (int, int) a_pair(int n)
{
    return {n, n * 2};
}

async (int, int)!error a_parse(int n)
{
    if (n < 0)
        err(9, "neg");
    return {n, n + 1};
}

/* await 之后直接解构 */
async int a_use(void)
{
    (int a, int b) = await a_pair(21);

    return a + b;
}

/* 两次 await，各自丢弃一个元素 */
async int a_drop(void)
{
    (int a, _) = await a_pair(5);
    (_, int b) = await a_pair(7);

    return a + b;
}

/* 解构赋值形式 */
async int a_assign(void)
{
    int a = 0, b = 0;

    (a, b) = await a_pair(3);
    return a * 10 + b;
}

/* 嵌套：await 的结果再喂给下一次 await */
async (int, int) a_nested(void)
{
    (int, int) t = await a_pair(1);

    return await a_pair(t[1]);
}

/* ---- 测试块 ---- */

test "async 函数可以返回元组" {
    struct a_pair_task *t = a_pair(5);

    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val[0] != 5 || t->val[1] != 10)
        err(2, "任务里的元组不对");
    if ((int)sizeof(t->val) != 8)
        err(3, "任务里的元组 sizeof 不对");
}

test "await 元组并解构" {
    struct a_use_task *t = a_use();

    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 63)
        err(2, "await 解构结果不对");
}

test "await 元组时可以丢弃元素" {
    struct a_drop_task *t = a_drop();

    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 19)
        err(2, "丢弃元素之后的结果不对");
}

test "解构赋值也能接 await 的结果" {
    struct a_assign_task *t = a_assign();

    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 36)
        err(2, "解构赋值结果不对");
}

test "await 的元组可以继续参与表达式" {
    struct a_nested_task *t = a_nested();

    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val[0] != 2 || t->val[1] != 4)
        err(2, "嵌套 await 不对");
}

test "'!error' + async + 元组" {
    struct a_parse_task *t = a_parse(3);
    struct a_parse_task *u = a_parse(-1);

    if (__c6_co_wait(t) != 0)
        err(1, "正常路径失败");
    if (t->val[0] != 3 || t->val[1] != 4)
        err(2, "正常路径的值不对");
    if (__c6_co_wait(u) != -1)
        err(3, "失败路径没有报告错误");
}
