/* tests/abi/method.c6 的纯 C 一侧（纯 C：`.c` 后缀，扩展不生效）。 */

struct PRect { int w, h; };
struct CRect { int w, h; };
int c6_area(struct CRect *r);               /* c6 一侧的方法，按原名调用 */

int plain_area(struct PRect *r) { return r->w * r->h; }

int plain_check(void)
{
    struct CRect c = { 4, 5 };
    return c6_area(&c);
}
