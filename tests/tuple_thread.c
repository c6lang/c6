/* c6 自测：元组 + spawn（OS 线程）
 *
 * 'spawn f(args)' 把实参按值拷到堆上的参数块里；元组和 struct 一样是值类型，
 * 所以可以作为实参穿过线程边界。线程里也可以照常调用返回元组的函数。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/tuple_thread.c
 */
#include <stdio.h>

void __c6_th_join_all(void);

/* 线程要写的结果（每个线程一个槽，免得竞争） */
static int got_a = 0, got_b = 0, got_ok = 0;

(int, int) split(int n)
{
    return {n / 2, n - n / 2};
}

/* 线程体：按值收一个元组 */
void th_take((int, int) t)
{
    got_a = t[0];
    got_b = t[1];
    got_ok = 1;
}

/* 线程体：在别的线程里调用返回元组的函数 */
void th_call(int n)
{
    (int a, int b) = split(n);

    got_a = a;
    got_b = b;
    got_ok = 1;
}

/* 线程体：在别的线程里解构赋值 */
void th_assign(int n)
{
    int a = 0, b = 0;

    (a, b) = split(n);
    got_a = a;
    got_b = b;
    got_ok = 1;
}

/* ---- 测试块 ---- */

test "spawn 可以把元组按值传给线程" {
    (int, int) t = {5, 6};

    got_a = 0;
    got_b = 0;
    got_ok = 0;
    spawn th_take(t);
    __c6_th_join_all();
    if (!got_ok)
        err(1, "线程没有跑起来");
    if (got_a != 5 || got_b != 6)
        err(2, "线程收到的元组不对");
}

test "线程里调用返回元组的函数" {
    got_a = 0;
    got_b = 0;
    got_ok = 0;
    spawn th_call(11);
    __c6_th_join_all();
    if (!got_ok)
        err(1, "线程没有跑起来");
    if (got_a != 5 || got_b != 6)
        err(2, "线程里解构出来的值不对");
}

test "线程里用解构赋值" {
    got_a = 0;
    got_b = 0;
    got_ok = 0;
    spawn th_assign(20);
    __c6_th_join_all();
    if (!got_ok)
        err(1, "线程没有跑起来");
    if (got_a != 10 || got_b != 10)
        err(2, "线程里解构赋值的结果不对");
}

test "spawn 的实参在 spawn 语句处求值一次" {
    (int, int) t = {1, 2};

    got_a = 0;
    got_b = 0;
    got_ok = 0;
    spawn th_take(t);
    t[0] = 99;                  /* 之后改局部变量不影响线程 */
    t[1] = 98;
    __c6_th_join_all();
    if (got_a != 1 || got_b != 2)
        err(1, "元组实参没有在 spawn 语句处按值复制");
}
