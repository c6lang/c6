/* ABI 兼容：结构体方法的符号改名不影响纯 C 一侧。
 *
 * c6 一侧把方法输出成 `<结构体>_<方法>`，源码名是弱别名；所以
 *   - 纯 C 定义、c6 调用的普通函数（`plain_area(&p)`）照旧按原名链接；
 *   - c6 定义的方法，纯 C 一侧按原名调用（`c6_area(&c)`）也照旧。
 * 两边都不走 `p->f()`，那是 c6 专有的调用语法。
 */
struct PRect { int w, h; };                 /* 由纯 C 一侧定义 */
int plain_area(struct PRect *r);
int plain_check(void);                      /* 纯 C 一侧，会回调 c6_area */

struct CRect { int w, h; };
int c6_area(struct CRect *r) { return r->w * r->h * 2; }

// EXIT 0
int main(void)
{
    struct PRect p = { 2, 3 };
    struct CRect c = { 4, 5 };

    if (plain_area(&p) != 6)
        return 1;
    if (c6_area(&c) != 40)
        return 2;
    if (plain_check() != 40)
        return 3;
    return 0;
}
