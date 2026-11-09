/* c6 编译期诊断：函数字面量 'RET (参数) { 体 }'
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过并运行成功。
 *
 * 字面量的值就是函数指示符，体在翻译单元末尾单独编译：体里只看得见它
 * 自己的参数和文件作用域的名字。所以写在外层的局部变量、形参、块作用域
 * 的 typedef 和 tag 都不可见——引用它们是编译期错误，而不是悄悄读到一个
 * 悬空的栈槽。
 */

// EXPECT test_capture_local: error: a function literal cannot capture 'x': without captures it is a plain function pointer, so its body sees only its parameters and the file scope (pass the value in as a parameter)
// EXPECT test_capture_param: error: a function literal cannot capture 'p': without captures it is a plain function pointer, so its body sees only its parameters and the file scope (pass the value in as a parameter)
// EXPECT test_capture_local_typedef: error: a function literal cannot capture 'LocalInt': without captures it is a plain function pointer, so its body sees only its parameters and the file scope (pass the value in as a parameter)
// EXPECT test_capture_local_tag: error: a function literal cannot capture 'L': without captures it is a plain function pointer, so its body sees only its parameters and the file scope (pass the value in as a parameter)
// EXPECT test_capture_shadowed_global: error: a function literal cannot capture 'v': without captures it is a plain function pointer, so its body sees only its parameters and the file scope (pass the value in as a parameter)
// EXPECT test_storage: error: a function literal cannot have a storage class
// EXPECT test_async: error: a function literal cannot be 'async'
// EXPECT test_no_body: error: '{' expected: the body of the function literal
// EXPECT test_named: error: a function literal has no name: 'named'

static int v = 1;

#if defined test_capture_local
int main(void)
{
    int x = 2;
    int (*f)(void) = int() { return x; };

    return f();
}
#elif defined test_capture_param
static int use(int p)
{
    int (*f)(void) = int() { return p; };

    return f();
}
int main(void)
{
    return use(1);
}
#elif defined test_capture_local_typedef
int main(void)
{
    typedef int LocalInt;
    LocalInt (*f)(void) = LocalInt() {
        LocalInt w = 1;

        return w;
    };

    return f();
}
#elif defined test_capture_local_tag
int main(void)
{
    struct L { int n; };
    int (*f)(void) = int() {
        struct L l = { 1 };

        return l.n;
    };

    return f();
}
#elif defined test_capture_shadowed_global
int main(void)
{
    int v = 3;
    int (*f)(void) = int() { return v; };

    return f();
}
#elif defined test_storage
int main(void)
{
    void (*f)(void) = static void() { };

    f();
    return 0;
}
#elif defined test_async
int main(void)
{
    void (*f)(void) = async void() { };

    f();
    return 0;
}
#elif defined test_no_body
int main(void)
{
    void(int x);

    return 0;
}
#elif defined test_named
int main(void)
{
    void (*f)(void) = void named(void) { };

    f();
    return 0;
}
#else
/* 正常路径：没有捕获，参数来自形参表，其余来自文件作用域 */
int bump(int k)
{
    int (*f)(int, int) = int(int n, int step) { return n + v + step; };

    return f(10, k);
}

int main(void)
{
    if (bump(1) != 12)
        return 1;
    return 0;
}
#endif
