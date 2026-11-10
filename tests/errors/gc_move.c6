/* c6 编译期诊断：move() 交出去的槽不能再读（P6）
 *
 * 运行（仓库根目录）：
 *   tests/run-tests.sh errors/gc_move
 *
 * 'move(a)' 把槽里的那个引用交出去，槽随之变空。空槽再读、再比较、再传参、
 * 再 move，拿到的都是空值：解引用就是运行期崩溃，传下去也会让别处踩空。所以
 * 编译器当场拒绝（E-GC17）：交出之后必须重新赋值，赋值之后照常使用。
 *
 * 允许的写法见正常路径：move() 之后 'a = new Node' / 'a = NULL' 重新赋值；
 * sizeof(a) 不读值；'defer' 体在作用域退出时才跑，它的 move 不影响后面的
 * 直线代码。
 */
#include <stdio.h>

struct Node { int v; };

static void take(@Node n)
{
    (void)n;
}

// EXPECT deref: error: 'a' was moved: assign a new reference before using it
// EXPECT compare: error: 'a' was moved: assign a new reference before using it
// EXPECT pass: error: 'a' was moved: assign a new reference before using it
// EXPECT again: error: 'a' was moved: assign a new reference before using it
// EXPECT ret: error: 'a' was moved: assign a new reference before using it
// EXPECT branch: error: 'a' was moved: assign a new reference before using it
// EXPECT loop: error: 'a' was moved: assign a new reference before using it
// EXPECT switch_case: error: 'a' was moved: assign a new reference before using it
// EXPECT not_managed: error: 'move' needs a managed reference held in a slot, not a 'int'

#if defined deref
int main(void)
{
    @Node a = new Node;
    move(a);
    return a->v;                   /* 空槽解引用：编译期就拦下来 */
}
#elif defined compare
int main(void)
{
    @Node a = new Node;
    move(a);
    if (a == NULL)                 /* 读空槽 */
        return 1;
    return 0;
}
#elif defined pass
int main(void)
{
    @Node a = new Node;
    move(a);
    take(a);                       /* 把空槽传出去 */
    return 0;
}
#elif defined again
int main(void)
{
    @Node a = new Node;
    move(a);
    move(a);                       /* 第二次 move 拿到的是空槽 */
    return 0;
}
#elif defined ret
static @Node give(void)
{
    @Node a = new Node;
    move(a);
    return a;                      /* 空槽不能当返回值交出去 */
}
int main(void)
{
    @Node p = give();
    (void)p;
    return 0;
}
#elif defined branch
static int k = 1;
int main(void)
{
    @Node a = new Node;
    move(a);
    if (k)                         /* 只有一条路重新赋值：另一条路还是空槽 */
        a = new Node;
    return a->v;
}
#elif defined loop
static int n = 1;
int main(void)
{
    @Node a = new Node;
    move(a);
    while (n-- > 0)                /* 循环可能一次都不跑 */
        a = new Node;
    return a->v;
}
#elif defined switch_case
static int k = 1;
int main(void)
{
    @Node a = new Node;
    move(a);
    switch (k) {
    case 0: a = new Node; break;
    default: break;                /* 这条路留着空槽 */
    }
    return a->v;
}
#elif defined not_managed
int main(void)
{
    int x = 1;
    move(x);                       /* 普通局部量没有引用可交 */
    return 0;
}
#else
/* 正常路径：交出、重新赋值、sizeof、defer 都必须通过 */
static @Node give(void)
{
    @Node a = new Node;
    a->v = 7;
    return move(a);                /* 交出之后立刻返回：没有读空槽 */
}

static int k = 1;

int main(void)
{
    unsigned long base = __c6_gc_live();
    @Node a = new Node;

    a->v = 1;
    if (sizeof(a) != sizeof(void *))    /* sizeof 不读值 */
        return 1;
    move(a);                            /* 交出：a 变空 */
    a = new Node;                       /* 重新赋值之后可以再用 */
    a->v = 2;
    if (a->v != 2)
        return 2;
    move(a);
    a = NULL;                           /* 空槽重新赋值成空值也是赋值 */
    a = new Node;
    a->v = 3;
    {
        @Node b = new Node;
        defer { move(b); }              /* defer 体单独分析，不漏到下面 */
        b->v = 4;
        if (b->v != 4)
            return 3;
    }                                   /* defer 在这里交出 b */
    if (k) {
        @Node c = new Node;
        move(c);
        c = new Node;                   /* 两条路都重新赋值 */
        c->v = 5;
        if (c->v != 5)
            return 4;
        c = NULL;
    }
    {
        @Node d = give();
        if (d->v != 7)
            return 5;
        d = NULL;
    }
    a = NULL;
    return __c6_gc_live() == base ? 0 : 6;
}
#endif
