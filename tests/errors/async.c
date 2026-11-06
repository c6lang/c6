/* c6 编译期诊断：async / await
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的"正常路径"，必须编译通过。
 */

// EXPECT test_outside: error: 'await' is only allowed inside an 'async' function
// EXPECT test_non_async: error: 'await' requires a call to an 'async' function
// EXPECT test_async_main: error: 'main' cannot be async
// EXPECT test_with_inline: error: 'async' cannot be combined with 'inline'
// EXPECT test_io_shape: error: 'c6_io' can only be used as 'await c6_io(fd, ev)'
// EXPECT test_in_defer: error: 'await' is not allowed in a 'defer' body
// EXPECT test_bad_signature: error: 'async' does not support this signature
// EXPECT test_nested: error: 'async' functions must have file scope
// EXPECT test_co_outside: error: 'co' can only be used inside a function
// EXPECT test_task_outside: error: 'task' can only be used inside a function
// EXPECT test_co_non_async: error: 'co' requires an 'async' function: 'plain' is a plain function (use 'go')
// EXPECT test_co_value: error: the operand of 'co' must be the handle of an 'async' call
// EXPECT test_co_call_non_async: error: the operand of 'co' must be the handle of an 'async' call
// EXPECT test_task_init: error: the initializer of a 'task' variable must be a call of an 'async' function
// EXPECT test_task_no_init: error: '=' expected
// EXPECT test_async_method: error: 'async' functions cannot be methods

#if defined test_outside
/* await 只能出现在 async 函数里 */
int main(void)
{
    int v = 0;
    v = await v;
    return v;
}
#elif defined test_non_async
/* await 的操作数必须是一次 async 调用（或 c6_io） */
int plain(void)
{
    return 1;
}
async int f(void)
{
    int v = 0;
    await plain();
    return v;
}
int main(void)
{
    return 0;
}
#elif defined test_async_main
/* 程序入口不能是协程 */
async int main(void)
{
    return 0;
}
#elif defined test_with_inline
/* async 和 inline 不能同时用 */
async inline int f(void)
{
    return 0;
}
int main(void)
{
    return 0;
}
#elif defined test_io_shape
/* c6_io 只能写成 await c6_io(fd, ev) */
async int f(void)
{
    return c6_io(0, C6_CO_EV_READ);
}
int main(void)
{
    return 0;
}
#elif defined test_in_defer
/* defer 体要能同步执行完，不能挂起 */
async int f(void)
{
    return 0;
}
async int g(void)
{
    defer { await f(); }
    return 0;
}
int main(void)
{
    return 0;
}
#elif defined test_bad_signature
/* 函数指针参数无法用"类型 + 名字"写回任务结构 */
async int f(int (*cb)(int))
{
    return cb(1);
}
int main(void)
{
    return 0;
}
#elif defined test_nested
/* 协程体必须定义在文件作用域 */
int outer(void)
{
    async int inner(void)
    {
        return 1;
    }
    return 0;
}
int main(void)
{
    return 0;
}
#elif defined test_co_outside
/* co 是语句，只能写在函数里 */
async int f(void)
{
    return 0;
}
co f();
int main(void)
{
    return 0;
}
#elif defined test_task_outside
/* task 是声明，只能写在函数里 */
async int f(void)
{
    return 0;
}
task t = f();
int main(void)
{
    return 0;
}
#elif defined test_co_non_async
/* co 的操作数必须是一次 async 调用 */
int plain(void)
{
    return 1;
}
int main(void)
{
    co plain();
    return 0;
}
#elif defined test_co_value
/* 值不是 async 调用的句柄 */
int main(void)
{
    int v = 1;
    co v;
    return 0;
}
#elif defined test_co_call_non_async
/* 通过表达式的普通调用：走通用的句柄检查 */
typedef int (*fn)(void);
int plain(void)
{
    return 1;
}
int main(void)
{
    fn p = plain;
    co p();
    return 0;
}
#elif defined test_task_init
/* task 的初始化器必须是一次 async 调用 */
async int f(void)
{
    return 0;
}
int main(void)
{
    task t = 1;
    (void)t;
    return 0;
}
#elif defined test_task_no_init
/* task 声明必须带初始化器，类型从初始化器推出来 */
async int f(void)
{
    return 0;
}
int main(void)
{
    task t;
    return 0;
}
#elif defined test_async_method
/* 方法调用会被改写成普通调用，指向不存在的函数体 */
struct conn
{
    int fd;
};
async int read_from(struct conn *c)
{
    return c->fd;
}
int main(void)
{
    return 0;
}
#else
/* 正常路径 */
async int add(int a, int b)
{
    return a + b;
}

async int add_all(void)
{
    return await add(await add(1, 2), 3);
}

int main(void)
{
    struct add_all_task *t = add_all();
    int rc = __c6_co_wait(t);
    __c6_co_destroy(t);
    return rc == 0 ? 0 : 1;
}
#endif
