/* c6 自测：async 任务持有一份引用（引用计数 GC，阶段 P5）
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/gc_async.c
 *
 * 任务是独立于创建者的一段执行：参数被拷进 `struct NAME_task` 的前缀，那次
 * 拷贝 +1；任务被 `__c6_co_destroy()` 回收时由编译器发射的 `release` 钩子
 * −1。所以「先放手、后等待」是安全的 —— 任务自己撑着对象。body 里的形参是
 * **借用**（不计数、不释放），要留住它就拷进自有局部。
 */
#include <stdio.h>

struct Node { int v; };

/* ---- 1. 任务创建时 +1，回收时 −1 ---- */
async int t1_body(int k, shared @Node n)
{
    if (__c6_gc_refcount(n) != 2)          /* 调用者 1 + 任务 1 */
        return -1;
    return k + n->v;
}

test "任务前缀持有一份引用" {
    shared @Node p = new shared Node;
    p->v = 60;
    if (__c6_gc_live() != 1 || __c6_gc_refcount(p) != 1)
        err(1, "new 之后应当只有一个对象、一个引用");
    {
        struct t1_body_task *t = t1_body(2, p);
        if (__c6_gc_refcount(p) != 2)
            err(2, "任务创建后计数应当 +1");
        if (__c6_co_wait(t) != 0)
            err(3, "body 里读到的计数不对");
        if (t->val != 62)
            err(4, "返回值不对");
        __c6_co_destroy(t);
    }
    if (__c6_gc_refcount(p) != 1)
        err(5, "任务回收后计数应当 −1");
    if (__c6_gc_live() != 1)
        err(6, "对象不该被释放");
}

/* ---- 2. 创建者放手后任务仍然活着 ---- */
async int t2_body(int k, shared @Node n)
{
    int v = n->v + k;                          /* 只有任务那份引用 */

    if (__c6_gc_refcount(n) != 1)
        return -1;
    return v;
}

test "创建者放手后任务仍然活着" {
    shared @Node p = new shared Node;
    p->v = 61;
    {
        struct t2_body_task *t = t2_body(1, p);
        p = NULL;                          /* 创建者的那份没了 */
        if (__c6_gc_live() != 1)
            err(10, "任务还持有，对象不该消失");
        if (__c6_co_wait(t) != 0)
            err(11, "body 里读到的计数不对");
        if (t->val != 62)                  /* 61 + k */
            err(12, "body 没读到内容");
        __c6_co_destroy(t);
    }
    if (__c6_gc_live() != 0)
        err(13, "任务回收后对象应当释放");
}

/* ---- 3. body 里想留住形参就必须拷贝（形参是借用） ---- */
static shared @Node g_keep = NULL;

async int t3_body(int k, shared @Node n)
{
    shared @Node m = n;                    /* 拷进自有局部：+1 */
    g_keep = m;                            /* 再交出去：+1 */
    return __c6_gc_refcount(n);
}

test "形参是借用，拷贝才有自己的引用" {
    unsigned long base = __c6_gc_live();
    shared @Node p = new shared Node;
    p->v = 62;
    g_keep = NULL;
    {
        struct t3_body_task *t = t3_body(1, p);
        if (__c6_co_wait(t) != 0)
            err(20, "body 没有正常结束");
        /* body 里：调用者 1 + 任务 1 + m 1 + g_keep 1 = 4 */
        if (t->val != 4)
            err(21, "body 里交出之后计数应当是 4");
        __c6_co_destroy(t);
    }
    if (g_keep == NULL || g_keep->v != 62)
        err(22, "body 交出来的引用不能用");
    if (__c6_gc_refcount(g_keep) != 2)     /* p 和 g_keep */
        err(23, "任务回收后应当只剩 p 和 g_keep 两份");
    g_keep = NULL;
    if (__c6_gc_refcount(p) != 1)
        err(24, "放手后应当只剩 p");
    p = NULL;
    if (__c6_gc_live() != base)
        err(25, "全部放手后对象应当释放");
}

/* ---- 4. 托管返回值：任务的 val 槽持有一份 ---- */
async shared @Node t4_make(int v)
{
    shared @Node n = new shared Node;

    n->v = v;
    return n;                              /* 局部放手，任务留下 */
}

async shared @Node t4_make_fresh(int v)
{
    shared @Node n = new shared Node;

    n->v = v;                              /* 只在 body 里用，不返回 */
    return new shared Node;                /* 新引用直接转移进任务 */
}

test "托管返回值由任务的 val 槽持有" {
    {
        struct t4_make_task *t = t4_make(63);
        shared @Node r = NULL;

        if (__c6_co_wait(t) != 0)
            err(30, "body 没有正常结束");
        if (t->val == NULL || t->val->v != 63)
            err(31, "返回值不对");
        r = t->val;                        /* +1 */
        if (__c6_gc_refcount(r) != 2)
            err(32, "接管后计数应当是 2");
        __c6_co_destroy(t);                /* 任务放弃自己那份 */
        if (__c6_gc_refcount(r) != 1)
            err(33, "任务回收后计数应当 −1");
        r = NULL;
        if (__c6_gc_live() != 0)
            err(34, "放手后对象应当释放");
    }
    {
        struct t4_make_fresh_task *t = t4_make_fresh(64);

        if (__c6_co_wait(t) != 0)
            err(35, "body 没有正常结束");
        if (t->val == NULL)
            err(36, "转移进来的新引用不可用");
        t->val->v = 64;                    /* 可写：值确实转移过来了 */
        if (t->val->v != 64)
            err(37, "转移进来的对象不可写");
        if (__c6_gc_refcount(t->val) != 1)
            err(38, "新引用应当是唯一的一份");
        __c6_co_destroy(t);
        if (__c6_gc_live() != 0)
            err(39, "回收后 body 里的 n 和 val 都应当释放");
    }
}

/* ---- 5. 普通（非托管）参数照旧 ---- */
async int t5_body(int a, int b)
{
    return a * b;
}

test "非托管参数不受影响" {
    struct t5_body_task *t = t5_body(6, 7);

    if (__c6_co_wait(t) != 0)
        err(40, "body 没有正常结束");
    if (t->val != 42)
        err(41, "返回值不对");
    __c6_co_destroy(t);
}
