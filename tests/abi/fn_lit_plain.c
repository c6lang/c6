/* tests/abi/fn_lit.c6 的纯 C 一侧（纯 C：`.c` 后缀，扩展不生效）。
 *
 * 这里只看得见普通的函数指针：c6 的函数字面量到这一侧就是一个地址。
 */

static int (*saved)(int);

int plain_call(int (*f)(int), int v) { return f(v); }

int plain_store(int (*f)(int), int v)
{
    saved = f;
    return saved(v);
}

int plain_twice(int v) { return v * 2; }
