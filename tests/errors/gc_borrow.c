/* c6 编译期诊断：形参是借用、以及在哪儿才能带引用跨线程/跨帧（P5）
 *
 * 运行（仓库根目录）：
 *   tests/run-tests.sh errors/gc_borrow
 *
 * 三条规则：
 *   - 托管形参的槽由调用者（或任务的参数槽）拥有，被调者只是借用：给它赋值
 *     会把别人的引用减掉，直接释放一个还活着的对象（E-GC12）；
 *   - 'go' 起的是另一个线程，跨线程只能传 'shared @T'（E-GC10）；
 *   - 'async' 的引用参数/返回值会被拷进任务，同样必须 'shared'（E-GC10）。
 */
struct Node { int v; };

// EXPECT borrow_param: error: '@struct Node' is a borrow: it owns nothing, so it cannot be reassigned
// EXPECT borrow_async_param: error: 'shared @struct Node' is a borrow: it owns nothing, so it cannot be reassigned
// EXPECT go_plain: error: passing a reference to 'go' requires 'shared'
// EXPECT async_plain: error: passing a reference to 'async' requires 'shared'
// EXPECT async_ret_plain: error: an 'async' return value that carries a reference requires 'shared'
// EXPECT shared_new_plain: error: a 'shared' slot needs an object with an atomic count

#if defined borrow_param
void take(@Node n)
{
    n = NULL;                      /* 借来的槽不能改写 */
}
int main(void)
{
    return 0;
}
#elif defined borrow_async_param
async int body(int k, shared @Node n)
{
    if (k)
        n = NULL;                  /* 任务的前缀槽同样只是借用 */
    return k;
}
int main(void)
{
    return 0;
}
#elif defined go_plain
void worker(@Node n)
{
    (void)n;
}
int main(void)
{
    @Node p = new Node;
    go worker(p);                  /* 跨线程要 shared */
    return 0;
}
#elif defined async_plain
async int body(int k, @Node n)
{
    return k + n->v;
}
int main(void)
{
    return 0;
}
#elif defined async_ret_plain
async @Node body(void)
{
    return NULL;
}
int main(void)
{
    return 0;
}
#elif defined shared_new_plain
int main(void)
{
    shared @Node p = new Node;     /* 对象不是原子的，但槽是 shared */
    (void)p;
    return 0;
}
#else
/* 正常路径：借用的形参可以读，可以拷进自有局部；跨线程用 shared */
async int body(int k, shared @Node n)
{
    shared @Node m = n;            /* 拷贝：+1，退出时 −1 */
    return k + m->v;
}

void worker(shared @Node n)
{
    (void)n;
}

int main(void)
{
    shared @Node p = new shared Node;
    shared @Node q = new shared Node;
    int r = 0;

    p->v = 3;
    q->v = 4;
    {
        struct body_task *t = body(1, p);
        r = __c6_co_wait(t);
        if (r != 0)
            return 1;
        if (t->val != 4)
            return 2;
        __c6_co_destroy(t);
    }
    go worker(q);
    {
        int i = 0;
        for (i = 0; i < 20000 && __c6_gc_refcount(q) > 1; i++)
            ;
        if (__c6_gc_refcount(q) != 1)
            return 3;
    }
    p = NULL;
    q = NULL;
    return __c6_gc_live() == 0 ? 0 : 4;
}
#endif
