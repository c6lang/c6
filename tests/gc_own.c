/* c6 自测：own 独占链（引用计数 GC，阶段 P6）
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/gc_own.c
 *
 * `own @T` 是独占所有权：不计数、不共享，持有者死亡时整条链递归释放。它
 * 不产生计数环，所以 `own` 边不进环检测（§8.1），这也是链表/树不用 `weak`
 * 的合法写法。建立链只有两条路：直接 `new`，或者 `move(p)` 把槽里的引用
 * 交出来（交出后槽变空，作用域退出时无事可做）。
 */
#include <stdio.h>

struct Node { int v; own @Node next; };

/* 每个被释放的节点都跑一次析构：验证独占链是逐个释放的 */
static int freed = 0;
void drop(struct Node *self)
{
    freed++;
    (void)self;
}

struct Tree { int v; own @Tree left, right; };

test "own 链：new 直接接上" {
    unsigned long base = __c6_gc_live();
    {
        @Node head = new Node;

        head->v = 1;
        head->next = new Node;          /* 新引用直接进 own 槽 */
        head->next->v = 2;
        head->next->next = new Node;
        head->next->next->v = 3;
        if (head->next->next->v != 3)
            err(1, "链没接上");
        if (__c6_gc_live() != base + 3)
            err(2, "应当有三个对象");
        freed = 0;
    }
    if (freed != 3)
        err(3, "独占链没有逐个释放");
    if (__c6_gc_live() != base)
        err(4, "释放后不该有对象留下");
}

test "own 树：递归释放" {
    unsigned long base = __c6_gc_live();
    {
        @Tree root = new Tree;
        @Tree l = new Tree;
        @Tree r = new Tree;

        root->v = 1;
        l->v = 2;
        r->v = 3;
        root->left = move(l);           /* 交出：l 变空 */
        root->right = move(r);
        /* 交出之后源槽不可再读（E-GC17，tests/errors/gc_move.c 覆盖），
           要重新赋值才能再用 */
        l = new Tree;
        l->v = 4;
        if (l->v != 4)
            err(10, "重新赋值之后应当可以再用");
        l = NULL;
        if (root->left->v != 2 || root->right->v != 3)
            err(11, "子树没接上");
        if (__c6_gc_live() != base + 3)
            err(12, "应当有三个对象");
    }
    if (__c6_gc_live() != base)
        err(13, "整棵树没有释放");
}

test "move 之后源槽不再释放" {
    unsigned long base = __c6_gc_live();
    {
        @Node a = new Node;
        @Node b = new Node;

        a->v = 1;
        b->v = 2;
        b->next = move(a);              /* a 的那一份交给 b 的 own 槽 */
        a = NULL;                       /* 重新赋值（已经是空槽，什么也不做） */
        if (b->next->v != 1)
            err(20, "move 的值不对");
        if (__c6_gc_live() != base + 2)
            err(21, "move 不该改变对象个数");
    }
    if (__c6_gc_live() != base)
        err(22, "放手后应当全部释放");
}

test "own 与非托管字段混用" {
    unsigned long base = __c6_gc_live();
    {
        @Node head = new Node;

        head->v = 5;
        head->next = new Node;
        head->next->v = 6;
        if (head->v + head->next->v != 11)
            err(30, "内容不对");
    }
    if (__c6_gc_live() != base)
        err(31, "释放后不该有对象留下");
}
