/* c6 编译期诊断：async 里不能用 weak（引用计数 GC，阶段 P3）
 *
 * 运行（仓库根目录）：
 *   tests/run-tests.sh errors/async_weak
 *
 * async 函数的整个原生帧会被拷进任务、恢复时可能落在另一个地址上，而
 * 弱槽是指向槽本身的指针，所以 async 里既不能有 weak 局部，也不能有
 * weak 形参。
 */
struct Node { int v; };

// EXPECT weak_local: error: a 'weak' reference cannot be declared in an async function: its frame is moved to the task
// EXPECT weak_param: error: a 'weak' reference cannot be a parameter of an async function: its frame is moved to the task
// EXPECT weak_return: error: a 'weak' reference cannot be returned by an async function: its frame is moved to the task

#if defined weak_local
async void body(void)
{
    @Node p = new Node;
    weak @Node w = p;          /* 帧会被搬走 */
    p = NULL;
    (void)w;
}
int main(void)
{
    return 0;
}
#elif defined weak_param
async void body(int n, weak @Node w)
{
    (void)n;
    (void)w;
}
int main(void)
{
    return 0;
}
#elif defined weak_return
async weak @Node body(void)
{
    return NULL;
}
int main(void)
{
    return 0;
}
#else
/* 正常路径：async 里只有强引用是合法的 */
async void body(int n)
{
    @Node p = new Node;
    p->v = n;
    p = NULL;
}
int main(void)
{
    return 0;
}
#endif
