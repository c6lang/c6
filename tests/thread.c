/* c6 自测：spawn = OS 线程
 *
 * 'spawn f(a);' 把参数求值后复制到堆上的参数块，用 pthread_create 起一个
 * 线程，线程在跳板里把参数块解开再调用 f；语句本身立刻返回。线程由
 * 启动它的线程回收（main 结束时由编译器插入的 __c6_th_join_all()，
 * 别的线程在退出时），所以这里可以直接调用它来等。
 *
 * 运行（仓库根目录）：
 *   make selftest
 * 或单个文件：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/thread.c
 */
#include <stdio.h>
#include <string.h>

/* 第二个翻译单元 */
int tu_add(int a, int b);
int tu_last_sum(void);

/* 空转等待，有上界：出错时报失败而不是挂住 */
static int wait_nonzero(volatile int *p)
{
    int i = 0;

    for (i = 0; i < 20000000 && *p == 0; i++)
        ;
    return *p;
}

/* ---- 1. 立刻返回，线程真的并发 ---- */

static volatile int gate = 0;
static volatile int seen = 0;

static int wait_for_gate(int marker)
{
    int i = 0;

    for (i = 0; i < 20000000 && !gate; i++)
        ;
    seen = gate ? marker : -1;
    return 0;
}

test "spawn 立刻返回：线程等 gate 打开后才继续" {
    gate = 0;
    seen = 0;
    spawn wait_for_gate(7);
    gate = 1;                       /* 能走到这里就说明 spawn 没有阻塞 */
    (void)wait_nonzero(&seen);
    if (seen != 7)
        err(1, "线程没有并发跑起来");
    __c6_th_join_all();
}

/* ---- 2. 参数在 spawn 处求值并复制 ---- */

static volatile int copied = 0;

static int take_int(int v)
{
    copied = v;
    return 0;
}

test "参数在 spawn 处求值，之后改局部不影响线程" {
    int v = 5;

    copied = 0;
    spawn take_int(v);
    v = 99;
    __c6_th_join_all();
    if (copied != 5)
        err(1, "线程看到的不是 spawn 处的值");
}

static int calls = 0;

static int side(void)
{
    return ++calls;
}

test "参数里的副作用只发生一次" {
    calls = 0;
    copied = 0;
    spawn take_int(side());
    __c6_th_join_all();
    if (calls != 1 || copied != 1)
        err(1, "参数表达式求值次数不对");
}

/* ---- 3. 各种形状的参数 ---- */

struct big {
    int a, b, c, d, e;
};

static volatile int big_sum = 0;

static int take_big(struct big v, const char *s, int n)
{
    big_sum = v.a + v.b + v.c + v.d + v.e + (int)strlen(s) + n;
    return 0;
}

test "结构体、指针、整数一起按值传" {
    struct big v = { 0, 0, 0, 0, 0 };

    v.a = 1; v.b = 2; v.c = 3; v.d = 4; v.e = 5;
    big_sum = 0;
    spawn take_big(v, "abc", 10);
    v.a = 100;                      /* 线程已经拿到副本 */
    __c6_th_join_all();
    if (big_sum != 1 + 2 + 3 + 4 + 5 + 3 + 10)
        err(1, "结构体参数没有按值复制");
}

static volatile int noarg = 0;

static int no_args(void)
{
    noarg = 1;
    return 0;
}

test "无参函数也能 spawn" {
    noarg = 0;
    spawn no_args();
    __c6_th_join_all();
    if (noarg != 1)
        err(1, "无参线程没有跑");
}

static volatile int voided = 0;

static void void_fn(int v)
{
    voided = v;
}

test "返回 void 的函数也能 spawn" {
    voided = 0;
    spawn void_fn(3);
    __c6_th_join_all();
    if (voided != 3)
        err(1, "void 函数的线程没有跑");
}

/* ---- 4. 线程自己的 defer 和错误槽 ---- */

static volatile int dflag = 0;

static int thread_defer(int v)
{
    defer { dflag = v; }
    return 0;
}

test "线程里的 defer 在线程退出时执行" {
    dflag = 0;
    spawn thread_defer(9);
    __c6_th_join_all();
    if (dflag != 9)
        err(1, "线程的 defer 没有执行");
}

static volatile int ea = 0, eb = 0;

/* libtcc1 的错误槽接口（每个线程一份） */
void __tcc_err_set(int id, const char *msg);
int __tcc_err_id_get(void);
void __tcc_err_clear(void);

static int fail_in(int id)
{
    __tcc_err_set(id, "boom");
    if (id == 1)
        ea = __tcc_err_id_get();
    else
        eb = __tcc_err_id_get();
    __tcc_err_clear();
    return 0;
}

test "两个线程的错误槽互不干扰" {
    ea = 0;
    eb = 0;
    spawn fail_in(1);
    spawn fail_in(2);
    __c6_th_join_all();
    if (ea != 1 || eb != 2)
        err(1, "线程之间的错误槽串了");
}

/* ---- 5. 线程里的 async 协程 ---- */

static volatile int co_val = 0;

async int co_body(int v)
{
    co_val = v;
    return 0;
}

static int thread_co(int v)
{
    struct co_body_task *t = co_body(v);
    int rc = __c6_co_wait(t);

    __c6_co_destroy(t);
    return rc;
}

test "线程里可以跑 async 协程" {
    co_val = 0;
    spawn thread_co(21);
    __c6_th_join_all();
    if (co_val != 21)
        err(1, "线程里的协程没有跑到");
}

/* ---- 6. 线程里再 spawn ---- */

static volatile int nested = 0;

static int inner_fn(void)
{
    nested = 11;
    return 0;
}

static int outer_fn(void)
{
    spawn inner_fn();
    return 0;
}

test "线程里再 spawn：外层退出时等内层" {
    nested = 0;
    spawn outer_fn();
    __c6_th_join_all();
    if (nested != 11)
        err(1, "嵌套出去的线程没有被等完");
}

/* ---- 7. 一批线程 ---- */

static volatile int slots[16] = {0};

static int put_slot(int i)
{
    slots[i] = 100 + i;
    return 0;
}

test "16 个 spawn 并行跑完" {
    int i = 0, bad = 0;

    for (i = 0; i < 16; i++)
        slots[i] = 0;
    for (i = 0; i < 16; i++)
        spawn put_slot(i);
    __c6_th_join_all();
    for (i = 0; i < 16; i++)
        if (slots[i] != 100 + i)
            bad++;
    if (bad)
        err(1, "有线程没有跑完");
}

/* ---- 8. 跨翻译单元 ---- */

test "spawn 可以调用别的翻译单元里的函数" {
    spawn tu_add(2, 3);
    __c6_th_join_all();
    if (tu_last_sum() != 5)
        err(1, "跨 TU 的 spawn 结果不对");
}

/* ---- 9. 软关键字：普通位置还是标识符 ---- */

static int spawn = 1;               /* 变量名 */
static int task = 2;                /* 变量名 */
typedef int co;                     /* 类型名 */
static co co_typed = 3;
static int go = 4;                  /* 旧关键字名，照常当标识符 */

static int spawn_plus(int x)        /* 函数名 */
{
    return x + 1;
}

test "spawn/co/task 在标识符位置仍然是标识符" {
    int x = 0;

    spawn = spawn + 1;              /* 赋值，不是语句关键字 */
    x = spawn;
    if (x != 2)
        err(1, "变量 spawn");
    if (task != 2)
        err(2, "变量 task");
    if (co_typed != 3)
        err(3, "typedef co");
    if (spawn_plus(5) != 6)
        err(4, "函数名里带 spawn");
    {
        co local = 4;               /* typedef 名做类型 */
        if (local != 4)
            err(5, "局部 co");
    }
    goto done;                      /* 标签也不受影响 */
    err(6, "goto 没跳");
done:
    (void)0;
}

test "软关键字和真语句按形状区分" {
    int y = 0;

    y = spawn + 1;                  /* 'spawn' 后面是 '='：表达式 */
    if (y != 3)
        err(1, "spawn 做右值");
    {
        int spawn = 40;             /* 局部变量名 */
        spawn = spawn + 2;
        if (spawn != 42)
            err(2, "局部变量 spawn");
    }
    {
        int task = 3;
        task = task * 2;
        if (task != 6)
            err(3, "局部变量 task");
    }
}

test "旧关键字名 go 仍然只是标识符" {
    go = go + 1;                    /* 赋值：后面不是标识符 */
    if (go != 5)
        err(1, "变量 go");
    goto go;                        /* 标号也不受影响 */
    err(2, "goto go 没跳");
go:
    {
        int go = 40;                /* 局部变量，遮蔽全局的同名变量 */
        go = go + 2;
        if (go != 42)
            err(3, "局部变量 go");
    }
}
