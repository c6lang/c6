/* ABI 兼容：函数字面量的值就是一个普通的函数指针。
 *
 * 它不分配对象、不携带额外信息，所以可以
 *   - 交给纯 C 一侧保存（`plain_store`）并回调；
 *   - 从 c6 一侧传进纯 C 的函数里调用（`plain_call`）；
 *   - 和纯 C 的普通函数指针放在一起用（`c6_sum(plain_twice, ...)`）。
 *
 * 反过来，纯 C 一侧看不到字面量这个语法：它只看见一个函数地址。
 */
struct Ctx { int (*cb)(int); int total; };
int plain_call(int (*f)(int), int v);
int plain_store(int (*f)(int), int v);
int plain_twice(int v);

/* c6 一侧：把字面量存进结构体，再当场调用 */
int c6_drive(struct Ctx *c, int v)
{
    c->cb = int(int n) { return n * 3; };
    c->total = c->cb(v);
    return c->total;
}

/* c6 一侧：接受任意函数指针，包括纯 C 的 */
int c6_sum(int (*f)(int), int a, int b)
{
    return f(a) + f(b);
}

// EXIT 0
int main(void)
{
    struct Ctx c = { 0, 0 };

    if (c6_drive(&c, 4) != 12)
        return 1;
    if (plain_call(c.cb, 5) != 15)              /* C 回调 c6 的字面量 */
        return 2;
    if (plain_store(int(int n) { return n - 1; }, 6) != 5)
        return 3;                               /* 字面量交给 C 保存 */
    if (c6_sum(plain_twice, 2, 3) != 10)        /* C 的函数指针进 c6 */
        return 4;
    if (c6_sum(int(int n) { return n + 1; }, 2, 3) != 7)
        return 5;
    return 0;
}
