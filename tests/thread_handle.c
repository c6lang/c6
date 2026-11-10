/* c6 自测：线程句柄 'thread h = spawn f(...);'
 *
 * 'spawn f(x);' 是"起了就不管"：线程由启动它的线程在退出时统一 join。要等
 * 某一个线程，就留住它的句柄：
 *
 *   thread h = spawn work(1);   启动，并拿到这次调用的记录
 *   h->join();               阻塞到它结束，重复调用幂等
 *   h->done();               1 = 已经结束，不阻塞
 *   h->value();              被调函数的返回值（整数/指针）
 *
 * 句柄是启动者私有的：线程仍然由启动者回收，句柄只多持有一份记录，
 * 作用域退出时自动还回去（没 join 也不漏）。
 *
 * 运行（仓库根目录）：
 *   make selftest
 * 或单个文件：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/thread_handle.c
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* 空转等待，有上界：出错时报失败而不是挂住 */
static int wait_nonzero(volatile int *p)
{
    int i = 0;

    for (i = 0; i < 20000 && *p == 0; i++)
        usleep(1000);
    return *p;
}

/* ---- 1. 只等三个线程里的第二个 ---- */

static volatile int gate = 0;
static volatile int ran[4] = { 0, 0, 0, 0 };

static int slow(int id)
{
    while (!gate)
        usleep(1000);
    ran[id] = id + 1;
    return id * 100;
}

test "等三个线程里的第二个，返回值可用，其余各自收尾" {
    gate = 0;
    ran[1] = ran[2] = ran[3] = 0;
    thread h1 = spawn slow(1);
    thread h2 = spawn slow(2);
    thread h3 = spawn slow(3);
    gate = 1;                       /* 三个一起开跑 */
    if (h2->join() != 0)
        err(1, "join 失败");
    if (!h2->done())
        err(2, "join 之后 done() 应为 1");
    if (h2->value() != 200)
        err(3, "value() 没有拿回返回值");
    if (ran[2] != 3)
        err(4, "线程体没有跑到");
    /* 另外两个照样由启动者收尾 */
    if (h1->join() != 0 || h3->join() != 0)
        err(5, "其余线程 join 失败");
    if (ran[1] != 2 || ran[3] != 4)
        err(6, "其余线程没有跑到");
}

test "join() 幂等，done()/value() 与结果一致" {
    gate = 1;
    ran[0] = 0;
    thread h = spawn slow(0);
    if (wait_nonzero(&ran[0]) == 0)
        err(10, "线程没有跑起来");
    if (h->join() != 0)
        err(11, "第一次 join 失败");
    if (h->join() != 0)
        err(12, "第二次 join 应当也是 0（不重复 pthread_join）");
    if (!h->done())
        err(13, "done() 应为 1");
    if (h->value() != 0)
        err(14, "value() 应为 0");
}

/* ---- 2. 句柄先走，线程仍然跑完 ---- */

test "句柄走了线程还在跑，引用不漏" {
    {
        thread h = spawn slow(1);

        gate = 1;
        ran[1] = 0;
        (void)h;                    /* 块尾自动归还句柄那一份 */
    }
    if (wait_nonzero(&ran[1]) == 0)
        err(20, "放弃句柄之后线程没有跑完");
}

/* ---- 3. 线程里再 spawn 并 join 孙线程 ---- */

static volatile int inner_ran = 0;

static int leaf(int k)
{
    inner_ran = k;
    return k + 1;
}

static int spawn_inside(int k)
{
    thread g = spawn leaf(k);
    long long v = 0;

    if (g->join() != 0)
        return -1;
    v = g->value();
    return (int)v;
}

test "线程内部再 spawn 并 join 孙线程" {
    thread h = spawn spawn_inside(41);

    inner_ran = 0;
    if (h->join() != 0)
        err(30, "外层线程 join 失败");
    if (h->value() != 42)
        err(31, "孙线程的返回值没有传上来");
    if (inner_ran != 41)
        err(32, "孙线程没有跑");
}

/* ---- 4. 裸 spawn 照旧 ---- */

test "裸 spawn 不受影响，join_all 仍然等全部" {
    int i = 0;

    gate = 1;
    for (i = 1; i <= 3; i++)
        ran[i] = 0;
    for (i = 1; i <= 3; i++)
        spawn slow(i);
    __c6_th_join_all();
    if (ran[1] != 2 || ran[2] != 3 || ran[3] != 4)
        err(40, "裸 spawn 的线程没有跑完");
}
