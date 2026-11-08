/* c6 自测：托管引用的类型层（引用计数 GC，阶段 P0）
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/gc_type.c
 *
 * 这一阶段只验证“@T 就是一个带限定符的指针”：大小、声明、空值比较、
 * 形参与返回值、以及限定符组合。分配与对象访问从 P1 开始（tests/gc.c）。
 */
#include <stdio.h>

struct Node { int v; };
struct Holder { @Node n; weak @Node back; };

/* 形参与返回值 */
static int peek(@Node p) { return p ? p->v : -1; }
static @Node give(@Node p) { return p; }

/* 限定符组合都要能声明（c6 要求显式初始化） */
static weak @Node g_weak = NULL;
static shared @Node g_shared = NULL;
static own @Node g_own = NULL;

test "托管引用就是指针：大小与 ABI" {
    if (sizeof(@Node) != sizeof(Node *))
        err(1, "sizeof(@Node) 不是指针大小");
    if (sizeof(weak @Node) != sizeof(void *))
        err(2, "sizeof(weak @Node) 不是指针大小");
    if (sizeof(own @Node) != sizeof(void *))
        err(3, "sizeof(own @Node) 不是指针大小");
    if (sizeof(shared @Node) != sizeof(void *))
        err(4, "sizeof(shared @Node) 不是指针大小");
    if (sizeof(@int) != sizeof(int *))
        err(5, "内置类型的托管引用大小不对");
}

test "声明、NULL 初始化与比较" {
    @Node p = NULL;
    weak @Node w = NULL;
    own @Node o = NULL;
    shared @Node s = NULL;

    if (p != NULL || w != NULL || o != NULL || s != NULL)
        err(1, "托管引用没有零初始化");
    if (p)
        err(2, "'@Node' 的空判断不对");
    if (!w)
        ; /* 空语句：'!' 对托管引用同样可用 */
    else
        err(3, "!'weak @Node' 的空判断不对");
}

test "空值可以作形参、返回值与成员" {
    /* 含托管字段的结构体只能放在堆上（P2 的按值规则） */
    @Holder h = new Holder;

    if (peek(NULL) != -1)
        err(1, "'@T' 作形参（空值）不对");
    if (give(NULL) != NULL)
        err(2, "'@T' 作返回值（空值）不对");
    if (h->n != NULL || h->back != NULL)
        err(3, "含托管字段的结构体零初始化不对");
    if (g_weak != NULL || g_shared != NULL || g_own != NULL)
        err(4, "文件作用域的托管引用没有零初始化");
}

test "多个 '*': '@T' 之上再取指针仍是普通指针" {
    @Node p = NULL;
    @Node *pp = &p;

    if (*pp != NULL)
        err(1, "'@T *' 的解引用不对");
    if (sizeof(@Node *) != sizeof(void *))
        err(2, "'@T *' 不是指针大小");
    if (sizeof(weak @Node *) != sizeof(void *))
        err(3, "'weak @T *' 不是指针大小");
}
