/* c6 自测：析构方法 drop（引用计数 GC，阶段 P4）
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/gc_drop.c
 *
 * 'void drop(struct T *self)' 是 T 的析构：计数归零、对象真正被释放时跑
 * 一次，且在字段释放之前 —— 那时字段还活着，析构可以读它们。它不能失败、
 * 不能挂起、也不能手动调用：对象只由引用决定生死。
 */
#include <stdio.h>

struct Node { int v; };

/* 最简单的析构：记下自己跑过几次 */
struct Counter { int v; };
static volatile int counter_runs = 0;
static int counter_seen = 0;
void drop(struct Counter *self)
{
    counter_runs++;
    counter_seen = self->v;
}

/* 析构里读自己的托管字段（字段在析构之后才释放） */
struct Pair { @Node n; int v; };
static int pair_runs = 0;
static int pair_child_v = 0;
void drop(struct Pair *self)
{
    pair_runs++;
    if (self->n)
        pair_child_v = self->n->v;
}

/* 析构里释放另一个对象：重入 */
struct Linked { @Node n; int v; };
static int linked_runs = 0;
void drop(struct Linked *self)
{
    linked_runs++;
    self->n = NULL;             /* 计数归零，对象就地入队释放 */
}

/* 析构顺序：先父后子 */
static int order_id = 0;
static int first_order = 0, second_order = 0;
struct Inner2 { @Node n; };
void drop(struct Inner2 *self)
{
    (void)self;
    second_order = ++order_id;
}

void work(void);

test "析构在最后一次释放时跑一次" {
    counter_runs = 0;
    {
        @Counter c = new Counter;
        c->v = 41;
        if (counter_runs != 0)
            err(1, "对象还活着就跑了析构");
        if (__c6_gc_refcount(c) != 1)
            err(2, "计数不是 1");
    }
    if (counter_runs != 1)
        err(3, "析构没有跑，或者跑了不止一次");
    if (counter_seen != 41)
        err(4, "析构没拿到自己的内容");
    if (__c6_gc_live() != 0)
        err(5, "对象没有释放");
}

test "析构里能读自己的托管字段" {
    pair_runs = 0;
    pair_child_v = 0;
    {
        @Pair p = new Pair;
        p->n = new Node;
        p->n->v = 7;
        p->v = 1;
    }
    if (pair_runs != 1)
        err(1, "析构没有跑");
    if (pair_child_v != 7)
        err(2, "析构跑的时候字段已经被释放了：顺序反了");
    if (__c6_gc_live() != 0)
        err(3, "父子的对象没有全部释放");
}

test "析构里释放另一个对象" {
    linked_runs = 0;
    {
        @Linked l = new Linked;
        l->n = new Node;
        l->n->v = 5;
        if (__c6_gc_live() != 2)
            err(1, "活对象数不对");
    }
    if (linked_runs != 1)
        err(2, "析构没有跑");
    if (__c6_gc_live() != 0)
        err(3, "重入释放留下了对象");
}

test "两次引用只有最后一次触发析构" {
    counter_runs = 0;
    {
        @Counter c = new Counter;
        @Counter d = c;
        c->v = 9;
        if (__c6_gc_refcount(c) != 2)
            err(1, "计数不是 2");
        c = NULL;
        if (counter_runs != 0)
            err(2, "还有引用时跑了析构");
        d = NULL;
    }
    if (counter_runs != 1)
        err(3, "析构次数不对");
}

/* 数组：每个元素各跑一次析构，和 C++ 一样 */
static int element_runs = 0;
static int element_sum = 0;
struct Elem { int v; };
void drop(struct Elem *self)
{
    element_runs++;
    element_sum += self->v;
}

test "数组元素的析构逐个跑" {
    element_runs = 0;
    element_sum = 0;
    {
        @Elem e = new Elem[3];
        e[0].v = 1;
        e[1].v = 2;
        e[2].v = 3;
    }
    if (element_runs != 3)
        err(1, "析构次数不是元素个数");
    if (element_sum != 6)
        err(2, "析构没读到每个元素的内容");
    if (__c6_gc_live() != 0)
        err(3, "数组没有释放");
}

/* 空转等待，有上界：出错时报失败而不是挂住 */
static int wait_nonzero(volatile int *p)
{
    int i = 0;

    for (i = 0; i < 20000000 && *p == 0; i++)
        ;
    return *p;
}

test "析构在 go 线程里也跑" {
    counter_runs = 0;
    counter_seen = 0;
    go work();
    if (!wait_nonzero(&counter_runs))
        err(1, "go 线程里的析构没有跑");
    if (counter_seen != 33)
        err(2, "go 线程里的析构没拿到内容");
}

void work(void) {
    @Counter c = new Counter;
    c->v = 33;
}
