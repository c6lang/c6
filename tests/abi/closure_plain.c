/* tests/abi/closure.c6 的纯 C 一侧（纯 C：`.c` 后缀，扩展不生效）。
 *
 * 闭包在这一侧是不透明的：只有一个指针，和一个按 ABI 猜出来的
 * `{ code, env }` 布局。这里只保存、只检查字段非空，不调用。
 */
struct __c6_closure_any { void *code; void *env; };

static struct __c6_closure_any *saved;

long plain_hold(struct __c6_closure_any *c)
{
    saved = c;
    return (long)sizeof(*c);
}

int plain_seen(void)
{
    return saved != 0 && saved->code != 0 && saved->env != 0;
}
