/* c6 编译期诊断：spawn（OS 线程）
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的"正常路径"，必须编译通过并跑通。
 */

// EXPECT test_spawn_outside: error: 'spawn' can only be used inside a function
// EXPECT test_spawn_async: error: 'spawn' requires a plain function: 'a' is 'async'
// EXPECT test_spawn_variadic: error: 'spawn' does not support variadic functions: 'vf'
// EXPECT test_spawn_fallible: error: 'spawn' does not support fallible functions: 'ff'
// EXPECT test_spawn_value: error: the operand of 'spawn' must be a call of a plain function
// EXPECT test_spawn_funcptr: error: the operand of 'spawn' must be a call of a plain function
// EXPECT test_spawn_signature: error: 'spawn' does not support this signature: 'cbf'
// EXPECT test_old_go: error: 'go' was renamed to 'spawn'
// EXPECT test_old_go_handle: error: 'go' was renamed to 'spawn'
// EXPECT test_old_go_scope: error: 'go' was renamed to 'spawn'

#if defined test_spawn_outside
/* spawn 是语句，只能写在函数里 */
int f(void)
{
    return 0;
}
spawn f();
int main(void)
{
    return 0;
}
#elif defined test_spawn_async
/* async 函数用 co，不用 spawn */
async int a(void)
{
    return 0;
}
int main(void)
{
    spawn a();
    return 0;
}
#elif defined test_spawn_variadic
/* 参数个数不定就没法生成参数块 */
int vf(int x, ...)
{
    return x;
}
int main(void)
{
    spawn vf(1);
    return 0;
}
#elif defined test_spawn_fallible
/* 错误要带回到某个地方，线程里没人接 */
int!error ff(void)
{
    return 0;
}
int main(void)
{
    spawn ff();
    return 0;
}
#elif defined test_spawn_value
/* 值不是函数调用 */
int main(void)
{
    int v = 1;
    spawn v;
    return 0;
}
#elif defined test_spawn_funcptr
/* 通过函数指针的调用没有可生成的跳板 */
int plain(void)
{
    return 1;
}
int main(void)
{
    int (*p)(void) = plain;
    spawn p();
    return 0;
}
#elif defined test_spawn_signature
/* 参数类型写不回去（函数指针成员没法放进参数块） */
int one(int x)
{
    return x;
}
int cbf(int (*cb)(int))
{
    return cb(1);
}
int main(void)
{
    spawn cbf(one);
    return 0;
}
#elif defined test_old_go
/* 'go' 不再是关键字：在旧语句形状上给一句迁移提示 */
int w1(int x)
{
    return x;
}
int main(void)
{
    go w1(1);
    return 0;
}
#elif defined test_old_go_handle
int w2(int x)
{
    return x;
}
int main(void)
{
    thread h = go w2(1);

    h->join();
    return 0;
}
#elif defined test_old_go_scope
/* 文件作用域的旧写法同样是改名提示，不是 implicit int */
int w3(void)
{
    return 0;
}
go w3();
int main(void)
{
    return 0;
}
#else
/* 正常路径：软关键字做普通标识符，同时真的 spawn 一次 */
static int spawn = 1;
static int task = 2;
typedef int co;

/* 旧关键字名 'go' 仍然只是普通标识符 */
static int go = 5;

static int worker(int v)
{
    return v + 1;
}

int main(void)
{
    co typed = 3;

    spawn = spawn + 1;
    task = task + 1;
    go = go + 1;
    if (typed != 3 || task != 3 || go != 6)
        return 1;
    spawn worker(1);
    __c6_th_join_all();
    return spawn == 2 ? 0 : 2;
}
#endif
