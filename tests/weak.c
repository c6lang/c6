/* c6 自测：弱引用（引用计数 GC，阶段 P3）
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/weak.c
 *
 * `weak @T` 只观察、不计数。它不能直接解引用，必须先升级成计数引用
 * （`@T q = w;`，原子 try-incref）；目标死亡时槽会被同步置 NULL。
 */
#include <stdio.h>

struct Node { int v; };

/* 弱字段：结构体死亡时注销，目标可以活得更久 */
struct Holder { weak @Node back; int pad; };

static weak @Node g_weak = NULL;

test "弱引用不计数、可以升级" {
    @Node p = new Node;
    p->v = 7;
    weak @Node w = p;
    @Node q = NULL;

    if (__c6_gc_refcount(p) != 1)
        err(1, "弱引用不该增加计数");
    if (w == NULL)
        err(2, "弱引用不该是空");
    q = w;                      /* 升级：原子 try-incref */
    if (q == NULL || q->v != 7)
        err(3, "升级拿不到对象");
    if (__c6_gc_refcount(p) != 2)
        err(4, "升级没有计数");
    q = NULL;
    p = NULL;
    if (__c6_gc_live() != 0)
        err(5, "对象没有释放干净");
}

test "目标死亡后弱槽变成空" {
    @Node p = new Node;
    weak @Node w = p;

    p = NULL;                   /* 最后一个计数引用消失 */
    if (w != NULL)
        err(1, "目标死亡后弱槽没有置空");
    {
        @Node q = w;            /* 升级一个空弱槽：得到空值 */
        if (q != NULL)
            err(2, "空弱槽升级出了对象");
    }
    if (__c6_gc_live() != 0)
        err(3, "还有对象活着");
}

test "弱字段：结构体死亡后注销，目标活得更久" {
    @Node keep = new Node;
    {
        @Holder h = new Holder;
        h->back = keep;
        if (__c6_gc_refcount(keep) != 1)
            err(1, "弱字段增加了计数");
        if (h->back == NULL)
            err(2, "弱字段没有写进去");
    }
    if (__c6_gc_live() != 1)
        err(3, "Holder 没有释放");
    if (__c6_gc_refcount(keep) != 1)
        err(4, "弱字段的目标被影响了");

    /* 目标死亡时槽要失效 */
    weak @Node w = keep;
    keep = NULL;
    if (w != NULL)
        err(5, "目标死亡后弱槽没有置空");
    if (__c6_gc_live() != 0)
        err(6, "还有对象活着");
}

test "弱字段的升级" {
    @Holder h = new Holder;
    @Node p = new Node;
    p->v = 11;
    h->back = p;
    {
        @Node q = h->back;
        if (q == NULL || q->v != 11)
            err(1, "弱字段升级失败");
        if (__c6_gc_refcount(p) != 2)
            err(2, "弱字段升级没有计数");
    }
    if (__c6_gc_refcount(p) != 1)
        err(3, "升级出来的引用没有释放");
    h = NULL;
    p = NULL;
    if (__c6_gc_live() != 0)
        err(4, "还有对象活着");
}

test "全局弱引用与重新指向" {
    @Node a = new Node;
    @Node b = new Node;

    g_weak = a;
    if (g_weak != a)
        err(1, "全局弱引用没有写进去");
    g_weak = b;                 /* 重新指向：旧槽解注册 */
    if (g_weak != b)
        err(2, "全局弱引用没有重新指向");
    a = NULL;
    if (g_weak != b)
        err(3, "无关对象死亡影响了别的弱槽");
    b = NULL;
    if (g_weak != NULL)
        err(4, "目标死亡后全局弱槽没有置空");
    if (__c6_gc_live() != 0)
        err(5, "还有对象活着");
}

test "弱引用自己死亡时不影响目标" {
    @Node p = new Node;
    {
        weak @Node w = p;       /* 局部弱引用：离开作用域即注销 */
        (void)w;
    }
    if (__c6_gc_refcount(p) != 1)
        err(1, "局部弱引用的死亡影响了目标");
    p = NULL;
    if (__c6_gc_live() != 0)
        err(2, "还有对象活着");
}
