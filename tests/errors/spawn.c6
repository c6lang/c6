/* c6 编译期诊断：go（OS 线程）
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的"正常路径"，必须编译通过并跑通。
 */

// EXPECT test_go_outside: error: 'go' can only be used inside a function
// EXPECT test_go_async: error: 'go' requires a plain function: 'a' is 'async'
// EXPECT test_go_variadic: error: 'go' does not support variadic functions: 'vf'
// EXPECT test_go_fallible: error: 'go' does not support fallible functions: 'ff'
// EXPECT test_go_value: error: the operand of 'go' must be a call of a plain function
// EXPECT test_go_funcptr: error: the operand of 'go' must be a call of a plain function
// EXPECT test_go_signature: error: 'go' does not support this signature: 'cbf'

#if defined test_go_outside
/* go 是语句，只能写在函数里 */
int f(void)
{
    return 0;
}
go f();
int main(void)
{
    return 0;
}
#elif defined test_go_async
/* async 函数用 co，不用 go */
async int a(void)
{
    return 0;
}
int main(void)
{
    go a();
    return 0;
}
#elif defined test_go_variadic
/* 参数个数不定就没法生成参数块 */
int vf(int x, ...)
{
    return x;
}
int main(void)
{
    go vf(1);
    return 0;
}
#elif defined test_go_fallible
/* 错误要带回到某个地方，线程里没人接 */
int!error ff(void)
{
    return 0;
}
int main(void)
{
    go ff();
    return 0;
}
#elif defined test_go_value
/* 值不是函数调用 */
int main(void)
{
    int v = 1;
    go v;
    return 0;
}
#elif defined test_go_funcptr
/* 通过函数指针的调用没有可生成的跳板 */
int plain(void)
{
    return 1;
}
int main(void)
{
    int (*p)(void) = plain;
    go p();
    return 0;
}
#elif defined test_go_signature
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
    go cbf(one);
    return 0;
}
#else
/* 正常路径：软关键字做普通标识符，同时真的 go 一次 */
static int go = 1;
static int task = 2;
typedef int co;

static int worker(int v)
{
    return v + 1;
}

int main(void)
{
    co typed = 3;

    go = go + 1;
    task = task + 1;
    if (typed != 3 || task != 3)
        return 1;
    go worker(1);
    __c6_th_join_all();
    return go == 2 ? 0 : 2;
}
#endif
