/* c6 编译期诊断：引用计数 GC 的托管引用
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过并运行。
 */
struct Node { int v; };
struct Other { int v; };

// EXPECT adopt_pointer: error: cannot assign a 'struct Node *' to '@struct Node': a managed reference is only made by 'new', by another managed reference or by the empty value
// EXPECT own_from_borrow: error: 'own @struct Node' takes ownership: only a fresh reference ('new') or the empty value can be stored in it
// EXPECT compound_assign: error: '+=' is not supported for a managed reference
// EXPECT two_managed: error: cannot assign a 'weak @struct Node' to '@struct Node': a weak reference only observes, it must be upgraded to a counted one first

#if defined adopt_pointer
int main(void)
{
    struct Node n = { 1 };
    @Node p = &n;              /* 栈上的对象不能变成托管引用 */
    return p->v;
}
#elif defined own_from_borrow
int main(void)
{
    @Node p = new Node;
    own @Node o = p;           /* own 只能接管新引用 */
    return o->v;
}
#elif defined compound_assign
int main(void)
{
    @Node p = new Node;
    p += 1;                    /* 托管引用不能做指针算术 */
    return 0;
}
#elif defined two_managed
int main(void)
{
    @Node p = new Node;
    weak @Node w = p;
    p = w;                     /* 弱引用要显式提升 */
    return 0;
}
#else
/* 正常路径 */
int main(void)
{
    @Node p = new Node;
    @Node q = p;
    p = NULL;
    q = NULL;
    return __c6_gc_live() == 0 ? 0 : 1;
}
#endif
