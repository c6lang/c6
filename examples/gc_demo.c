/* 托管引用（引用计数 GC）用法演示
 *
 * 编译运行（在仓库根目录）：
 *   make build
 *   ./tcc/tcc -B tcc -std=c6 -run examples/gc_demo.c
 *
 * 四种限定符：
 *
 *   @T        强引用，计数；可以有多个，最后一个消失时释放
 *   shared @T 同上，但计数是原子的，可以跨线程；对象要用 new shared T 建
 *   weak @T   弱引用，不计数；解引用前必须先升级（@T q = w;）
 *   own @T    独占，不计数；持有者死亡时整条链/子树递归释放
 *
 * 本文件按五段演示：
 *   1. new / 作用域释放 / p = NULL / 计数 / new T[n] 逐元素析构
 *   2. own 子树 + weak 反向引用（树的标准写法）+ move()
 *   3. shared + go：引用跨线程，主线程先放手对象也还活着
 *   4. async 任务自己持有参数与返回值
 *   5. 收尾：__c6_gc_live() 归零
 *
 * 编译器会拒绝的写法（本文件里只以注释出现，因为编译不过）：
 *
 *   struct Node { @Node next; };          // 自环
 *   error: reference cycle: Node -next-> Node; break it with 'weak' or 'own'
 *
 *   void take(@Node n) { n = NULL; }      // 形参只是借用
 *   error: '@struct Node' is a borrow: it owns nothing, so it cannot be
 *          reassigned (copy it into an owned local first)
 *
 *   shared @Node p = new Node;            // 计数不是原子的
 *   error: a 'shared' slot needs an object with an atomic count: create it
 *          with 'new shared T', not 'new T'
 */
#include <stdio.h>
#include <unistd.h>

/* ---- 自检：任何一条不成立就返回非 0 ---- */
static int fails = 0;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("  FAIL: %s\n", what);
        fails++;
    }
}

/* 有上界的等待：出错时报失败，而不是挂住 */
static int wait_flag(volatile int *p)
{
    int i = 0;

    for (i = 0; i < 20000 && *p == 0; i++)
        usleep(1000);
    return *p;
}

static int wait_refcount(void *p, unsigned long long want)
{
    int i = 0;

    for (i = 0; i < 20000 && __c6_gc_refcount(p) > want; i++)
        usleep(1000);
    return __c6_gc_refcount(p) == want;
}

/* ---- 1. 作用域与析构：一个"资源"类型 ---- */
struct Buffer { int len; };

static int buffers_freed = 0;

/* void drop(struct T *self) 是 T 的析构：计数归零、对象真正释放时跑一次，
   在字段释放之前。不能失败、不能挂起、也不能手动调用。 */
void drop(struct Buffer *self)
{
    buffers_freed++;
    (void)self;
}

/* ---- 2. 树：own 撑住子树，weak 指回父亲 ---- */
struct Node { int v; own @Node left, right; weak @Node parent; };

static int nodes_freed = 0;

void drop(struct Node *self)
{
    nodes_freed++;
    (void)self;
}

/* ---- 3/4. 跨线程与跨帧的载荷 ---- */
struct Counter { int v, a, b; };

static shared @Counter g_handed = NULL;   /* 线程把引用交接给主线程 */
static volatile int g_a = 0;
static volatile int g_b = 0;

void bump(int which, shared @Counter c)
{
    if (which == 1) {
        c->a = 1;
        g_handed = c;             /* 存进全局托管槽：+1 */
        g_a = 1;
    } else {
        c->b = 2;
        g_b = 1;
    }
}

async int scale(int k, shared @Counter c)
{
    c->v = c->v + k;              /* 任务自己持有一份 c */
    return __c6_gc_refcount(c);
}

async shared @Counter mk_counter(int v)
{
    shared @Counter c = new shared Counter;

    c->v = v;
    return c;                     /* 返回值的槽（任务里的 val）持有它 */
}

int main(void)
{
    unsigned long long base = __c6_gc_live();

    /* ---- 1. new / 作用域释放 / p = NULL ---- */
    printf("1. new、作用域释放、p = NULL\n");
    {
        @Buffer a = new Buffer;
        @Buffer b = NULL;

        a->len = 3;
        b = a;                    /* 两个托管槽指向同一个对象 */
        check(__c6_gc_refcount(a) == 2, "两个引用时计数为 2");
        a = NULL;                 /* 放手一个（不需要 reset）：对象还活着 */
        check(__c6_gc_refcount(b) == 1, "放手后计数减一");
        b->len = 4;
        check(b->len == 4, "对象仍然可用");
    }                             /* b 也走了 → 释放，析构跑一次 */
    check(buffers_freed == 1, "析构跑了一次");
    check(__c6_gc_live() == base, "作用域里的对象已经释放");

    /* new T[n]：数组也是托管对象，释放时逐元素跑析构 */
    {
        @Buffer arr = new Buffer[3];

        buffers_freed = 0;
        arr[0].len = 1;
        arr[1].len = 2;
        arr[2].len = 3;
        check(arr[2].len == 3, "数组元素可写");
    }
    check(buffers_freed == 3, "三个元素各跑一次析构");
    check(__c6_gc_live() == base, "数组已经释放");

    /* ---- 2. own 子树 + weak 父指针 ---- */
    printf("2. own 子树、weak 反向引用、move()\n");
    {
        @Node root = new Node;
        @Node l = new Node;
        @Node r = new Node;

        root->v = 1;
        l->v = 2;
        r->v = 3;
        l->parent = root;         /* weak：只是观察，不持有 → 不成环 */
        r->parent = root;
        root->left = move(l);     /* own 只收 new 或 move()；交出后 l 是空槽 */
        root->right = move(r);
        check(l == NULL && r == NULL, "move() 之后源槽为空");
        check(root->left->v == 2 && root->right->v == 3, "子树接上了");

        {
            /* weak 不能直接解引用：先升级成计数引用再用 */
            @Node up = root->left->parent;
            check(up == root && up->v == 1, "weak 升级后能用");
        }                         /* up 在这里放手，root 不受影响 */

        nodes_freed = 0;
    }                             /* root 走 → 整棵树递归释放 */
    check(nodes_freed == 3, "own 子树逐个释放（析构跑三次）");
    check(__c6_gc_live() == base, "树已经释放");

    /* ---- 3. shared + go：引用跨线程 ---- */
    printf("3. shared 跨线程：主线程先放手，对象仍活着\n");
    {
        shared @Counter c = new shared Counter;

        c->v = 0;
        g_handed = NULL;
        g_a = 0;
        g_b = 0;
        go bump(1, c);            /* 实参块各持有一份 */
        go bump(2, c);
        check(wait_flag(&g_a) && wait_flag(&g_b), "两个线程都跑完了");
        /* 线程退出后实参块归还，只剩主线程与交接过来的两份 */
        check(wait_refcount(c, 2), "线程退出后应剩两份");
        check(c->a == 1 && c->b == 2, "两个线程写的内容都在");
        check(g_handed == c, "交接过来的就是同一个对象");
        g_handed = NULL;          /* 交接的那份放手 */
        check(__c6_gc_refcount(c) == 1, "只剩主线程一份");
    }
    check(__c6_gc_live() == base, "跨线程对象已经释放");

    /* ---- 4. async：任务持有参数与返回值 ---- */
    printf("4. async 任务持有参数与返回值\n");
    {
        shared @Counter c = new shared Counter;

        c->v = 10;
        {   /* 任务的句柄留在手上，块尾自动销毁 */
            task t = scale(3, c);          /* 参数进任务时 +1 */

            check(__c6_gc_refcount(c) == 2, "任务持有一份");
            __c6_co_wait(t);               /* 驱动到完成 */
            check(t->val == 2, "body 里看到的是调用者 + 任务两份");
            check(c->v == 13, "body 改到了对象");
        }
        check(__c6_gc_refcount(c) == 1, "任务销毁后只剩调用者");
    }
    check(__c6_gc_live() == base, "async 参数对象已经释放");

    {
        shared @Counter got = NULL;

        {   /* 返回值由任务的 val 槽持有，接管时 +1 */
            task t = mk_counter(7);

            __c6_co_wait(t);
            got = t->val;                  /* 接管 */
            check(__c6_gc_refcount(got) == 2, "接管后任务与局部各一份");
        }
        check(got != NULL && got->v == 7, "托管返回值可用");
        check(__c6_gc_refcount(got) == 1, "任务放弃自己那份");
        got = NULL;
    }
    check(__c6_gc_live() == base, "返回值对象已经释放");

    /* ---- 5. 收尾 ---- */
    printf("5. 收尾：live=%llu（进入 main 时 %llu）\n",
           __c6_gc_live(), base);
    check(__c6_gc_live() == base, "全程没有泄漏");

    if (fails) {
        printf("%d 项检查失败\n", fails);
        return 1;
    }
    printf("全部检查通过\n");
    return 0;
}
