/* c6 闭包进环：C66
 *
 * 环境在静态图里是「擦除」的（每个字面量一个，跨 TU 名字不稳定），所以环边
 * 建在**签名级**上：`closure int()` 捕获 `@Node`，就是
 * `closure_int_void → Node → closure_int_void`。`struct Node` 里放着同签名的
 * 闭包字段时，这条边就成环，计数回收永远放不掉它们——编译期直接报 C66。
 *
 * 边是保守的：同一签名的另一个字面量即使不捕获 Node 也会参与。
 */

// EXPECT cycle: error: reference cycle:
// EXPECT cycle: break it with 'weak' or 'own'

#if defined cycle
struct Node { closure int() f; };

int main(void)
{
    @Node p = new Node;
    p->f = int() { return p != NULL; };
    p->f = NULL;
    p = NULL;
    return 0;
}
#else
struct Plain { int v; };

int main(void)
{
    @Plain p = new Plain;
    p->v = 3;
    int r = p->v;
    p = NULL;
    return r != 3;
}
#endif
