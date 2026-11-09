/* c6 编译期诊断：引用计数 GC 的托管引用
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过并运行。
 */
struct Node { int v; };
struct Other { int v; };
struct Holder { @Node n; };
static void take(struct Holder h) { (void)h; }

// EXPECT adopt_pointer: error: cannot assign a 'struct Node *' to '@struct Node': a managed reference is only made by 'new', by another managed reference, by '@f".."' or by the empty value
// EXPECT own_from_borrow: error: 'own @struct Node' takes ownership: only a fresh reference ('new') or the empty value can be stored in it
// EXPECT compound_assign: error: '+=' is not supported for a managed reference
// EXPECT weak_deref: error: cannot use a 'weak @struct Node' directly: upgrade it first ('@T q = w;')
// EXPECT weak_return: error: a weak reference cannot be returned: upgrade it first ('@T q = w;')
// EXPECT by_value_local: error: 'struct Holder' holds managed references: a declared object is not possible. Allocate it with 'new' and hold it in '@struct Holder'
// EXPECT by_value_param: error: 'struct Holder' holds managed references: a parameter is not possible. Allocate it with 'new' and hold it in '@struct Holder'
// EXPECT by_value_store: error: 'struct Holder' holds managed references: an assignment is not possible. Allocate it with 'new' and hold it in '@struct Holder'
// EXPECT fresh_to_plain: error: a new reference cannot be stored in 'struct Node *': it is not a managed reference (declare it as '@T')
// EXPECT gc_union: error: 'union U' is a union with a managed field: which member is alive cannot be known, use a struct
// EXPECT gc_local_struct: error: 'struct Local' holds managed references and is defined in a block: define it at file scope
// EXPECT gc_flexible: error: 'struct Flex' has a flexible array of managed references: allocate it with 'new T[n]' instead

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
#elif defined weak_deref
int main(void)
{
    @Node p = new Node;
    weak @Node w = p;
    return w->v;               /* 弱引用要先升级才能用 */
}
#elif defined by_value_local
int main(void)
{
    struct Holder h = { NULL }; /* 含托管字段的结构体不能放在栈上 */
    return 0;
}
#elif defined by_value_param
int main(void)
{
    @Holder p = new Holder;
    take(*p);                  /* 也不能按值传参 */
    return 0;
}
#elif defined by_value_store
int main(void)
{
    @Holder p = new Holder;
    @Holder q = new Holder;
    *q = *p;                   /* 也不能按值赋值 */
    return 0;
}
#elif defined fresh_to_plain
int main(void)
{
    struct Node *p = new Node; /* 新引用存进普通指针就没有人释放了 */
    return 0;
}
#elif defined gc_union
union U { @Node n; int i; };
int main(void)
{
    union U u = { NULL };
    return 0;
}
#elif defined gc_local_struct
int main(void)
{
    struct Local { @Node n; } l = { NULL };
    return 0;
}
#elif defined gc_flexible
struct Flex { int n; @Node arr[]; };
int main(void)
{
    return 0;
}
#elif defined weak_return
weak @Node give(weak @Node w) { return w; }
int main(void)
{
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
