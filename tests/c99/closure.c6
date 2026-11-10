/* C99 输出后端：捕获式闭包（V2）
 *
 * 闭包在生成的 C 里就是 `struct __c6_closure_<hash> *`：隐藏对象里放
 * `{ code, env }`，`env` 是 `void *`；捕获、包装函数指针（跳板）、变参、
 * struct 返回、`!error`、托管捕获都是普通 C 代码 —— 生成的源码要过
 * `gcc -std=c99 -pedantic -Wall -Wextra -Werror` 且零告警。
 *
 * 运行（仓库根目录）：
 *   make c99test
 */
// EXIT 0
// OUTPUT add 7
// OUTPUT tramp 12
// OUTPUT struct 7 8
// OUTPUT vararg 6
// OUTPUT managed 11
// OUTPUT err 9
// OUTPUT live 0

#include <stdio.h>
#include <stdarg.h>

struct P { int x, y; };
struct Node { int v; };

static closure int(int) adder(int n)
{
    return int(int x) { return x + n; };
}

static int twice(int x) { return x * 2; }

/* catch 体要以 return 收尾，所以单独放一个函数里 */
static int catch_id(closure int!error(int) f)
{
    int v = f(-1) catch e { return e.id; };
    return v;
}

int main(void)
{
    int base = 3;
    closure int(int) f = adder(5);          /* 捕获形参 */
    closure int(int) t = twice;             /* 跳板 */
    closure struct P(int) mk = struct P(int a) {
        struct P q = {0};
        q.x = a + base;
        q.y = a * 2;
        return q;
    };
    closure int(int, ...) v = int(int n, ...) {
        va_list ap = {0};
        va_start(ap, n);
        int x = va_arg(ap, int);
        va_end(ap);
        return n + x + base;
    };
    closure int!error(int) e = int!error (int x) {
        if (x < 0)
            err(9, "negative");
        return x + base;
    };
    @Node p = new Node;
    p->v = 11;
    closure int() m = int() { return p->v; };   /* 托管捕获 */

    struct P q = mk(4);
    printf("add %d\n", f(2));
    printf("tramp %d\n", t(6));
    printf("struct %d %d\n", q.x, q.y);
    printf("vararg %d\n", v(1, 2));
    printf("managed %d\n", m());
    printf("err %d\n", catch_id(e));

    f = NULL;
    t = NULL;
    mk = NULL;
    v = NULL;
    e = NULL;
    m = NULL;
    p = NULL;
    printf("live %d\n", __c6_gc_live());
    return 0;
}
