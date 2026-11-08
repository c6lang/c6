/* c6 编译期诊断：线程句柄 'thread h = go f(...);'
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的"正常路径"，必须编译通过并跑通。
 */

// EXPECT test_thread_not_go: error: the initializer of a 'thread' variable must be 'go f(...)'
// EXPECT test_thread_drop: error: a destructor cannot suspend: it runs while the object is being released

#if defined test_thread_not_go
/* 句柄只能来自 'go'：普通调用给的是任务（task）或者干脆没有句柄 */
static int plain(int v)
{
    return v;
}
int main(void)
{
    thread h = plain(1);
    return h != 0;
}
#elif defined test_thread_drop
/* 析构里不能起线程：它跑在对象释放的路径上 */
struct Res { int fd; };
static int worker(int v)
{
    return v;
}
void drop(struct Res *self)
{
    thread h = go worker(1);

    h->join();
    (void)self;
}
int main(void)
{
    return 0;
}
#else
/* 正常路径：软关键字做普通标识符，同时真的用一次句柄 */
static int thread = 1;
static int go = 2;

static int worker(int v)
{
    return v + 1;
}

int main(void)
{
    int sum = 0;

    thread = thread + 1;        /* 不起线程：后面不是 'go' */
    go = go + 1;
    if (thread != 2 || go != 3)
        return 1;
    thread h = go worker(41);
    if (h->join() != 0)
        return 2;
    sum = (int)h->value();
    return sum == 42 ? 0 : 3;
}
#endif
