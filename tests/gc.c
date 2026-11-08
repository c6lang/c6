/* c6 自测：引用计数 GC（阶段 P1：分配、存储、释放）
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/gc.c
 *
 * 每个 test 块结束时它声明的托管局部量都已释放，所以下一个块开头的
 * __c6_gc_live() 必须是 0：这就是“没有泄漏”的断言。
 */
#include <stdio.h>

struct Node { int v; };

/* 把引用交出去：调用者拿到一个引用 */
static @Node give(void)
{
    @Node p = new Node;
    p->v = 5;
    return p;
}

/* 错误路径：块里的对象在 err() 传播时释放 */
static int!error boom(void)
{
    @Node p = new Node;
    p->v = 9;
    err(3, "boom");
    return 0;
}

static int take(@Node p)
{
    return p->v;
}

test "new 分配、零初始化、作用域退出自动释放" {
    if (__c6_gc_live() != 0)
        err(1, "进入时就有活对象");
    {
        @Node p = new Node;
        if (p == NULL)
            err(2, "new 返回了空值");
        if (p->v != 0)
            err(3, "new 没有把对象清零");
        if (__c6_gc_refcount(p) != 1)
            err(4, "新对象的计数不是 1");
        if (__c6_gc_live() != 1)
            err(5, "活对象数不是 1");
        p->v = 7;
        if (p->v != 7)
            err(6, "对象写不进去");
    }
    if (__c6_gc_live() != 0)
        err(7, "离开作用域后对象没有释放");
}

test "p = NULL 释放；重复 NULL 无害" {
    @Node p = new Node;
    if (__c6_gc_live() != 1)
        err(1, "分配失败");
    p = NULL;
    if (p != NULL)
        err(2, "NULL 没有写进槽");
    if (__c6_gc_live() != 0)
        err(3, "'p = NULL' 没有释放对象");
    p = NULL;
    if (__c6_gc_live() != 0)
        err(4, "对空槽再赋 NULL 影响了计数");
}

test "共享引用：p = q 计数加一，自赋值净变化为 0" {
    @Node p = new Node;
    {
        @Node q = p;
        if (__c6_gc_refcount(p) != 2)
            err(1, "'@Node q = p' 没有加计数");
        if (__c6_gc_live() != 1)
            err(2, "同一对象被算了两次");
        p = p;
        if (__c6_gc_refcount(p) != 2)
            err(3, "自赋值改变了计数");
        q = NULL;
        if (__c6_gc_refcount(p) != 1)
            err(4, "释放 q 后计数不是 1");
    }
    if (__c6_gc_live() != 1)
        err(5, "p 还在却被释放了");
}

test "return 把引用交给调用者" {
    {
        @Node r = give();
        if (r->v != 5)
            err(1, "返回值不对");
        if (__c6_gc_live() != 1)
            err(2, "返回后活对象数不是 1");
        if (__c6_gc_refcount(r) != 1)
            err(3, "调用者拿到的计数不是 1");
    }
    if (__c6_gc_live() != 0)
        err(4, "调用者的引用没有释放");
}

test "err() 传播时释放局部对象" {
    boom() catch e {
        if (__c6_gc_live() != 0)
            err(1, "错误路径没有释放对象");
        if (e.id != 3)
            err(2, "错误码没有传出来");
        return 0;               /* 测试块返回 0 表示这一块还没失败 */
    };
    err(3, "boom() 没有失败");
}

test "丢弃的 new 立刻释放" {
    new Node;
    if (__c6_gc_live() != 0)
        err(1, "丢弃的 new 泄漏了");
}

test "new 作实参：调用返回后释放" {
    int v = take(new Node);
    if (v != 0)
        err(1, "实参对象的初值不是 0");
    if (__c6_gc_live() != 0)
        err(2, "实参对象在调用后没有释放");
}

test "own @T：独占，退出作用域销毁" {
    {
        own @Node o = new Node;
        o->v = 4;
        if (__c6_gc_live() != 1)
            err(1, "own 对象没有被分配");
        if (__c6_gc_refcount(o) != 1)
            err(2, "own 对象的计数不是 1");
    }
    if (__c6_gc_live() != 0)
        err(3, "own 对象没有销毁");
}

test "weak @T：对象死了就变空" {
    @Node p = new Node;
    weak @Node w = p;
    if (w != p)
        err(1, "弱引用没有指向对象");
    if (__c6_gc_refcount(p) != 1)
        err(2, "弱引用不该加计数");
    p = NULL;
    if (w != NULL)
        err(3, "对象释放后弱引用没有变空");
    if (__c6_gc_live() != 0)
        err(4, "对象没有释放");
}

test "循环里声明：每次迭代都释放" {
    @Node keep = new Node;
    int i = 0;
    for (i = 0; i < 1000; i++) {
        @Node q = new Node;
        if (q == NULL)
            err(1, "迭代里分配失败");
    }
    if (__c6_gc_live() != 1)
        err(2, "循环里的对象没有逐个释放");
    keep = NULL;
    if (__c6_gc_live() != 0)
        err(3, "最后一个对象没有释放");
}
