/* c6 闭包不能捕获的东西（C94）
 *
 * 捕获是**按值拷贝**：`own`/`weak` 引用、块作用域 `static`、数组、VLA、
 * 块作用域里定义的 tag、带 `drop` 或托管字段的结构体、thread/task 句柄
 * 都不能拷（要么语义上不成立，要么会多释放一次）。需要这些值时把
 * `@T` owner、切片或裸指针传进去。
 */

// EXPECT own_ref: error: a closure cannot capture 'p': an 'own' reference cannot be copied
// EXPECT weak_ref: error: a closure cannot capture 'w': upgrade the 'weak' reference to '@T' first
// EXPECT static_local: error: a closure cannot capture the block-scope 'static' 's': move it to file scope
// EXPECT array: error: a closure cannot capture the array 'a': pass a slice 'T[:]' or a pointer instead
// EXPECT block_type: error: a closure cannot capture 'v': its type is declared in a block (move the type to file scope)

struct Node { int v; };

#if defined own_ref
int main(void)
{
    own @Node p = new Node;
    closure int() f = int() { return p->v; };
    return f() != 0;
}
#elif defined weak_ref
int main(void)
{
    @Node p = new Node;
    weak @Node w = p;
    closure int() f = int() { return w != NULL; };
    return f() != 1;
}
#elif defined static_local
int main(void)
{
    static int s = 3;
    closure int() f = int() { return s; };
    return f() != 3;
}
#elif defined array
int main(void)
{
    int a[4] = {0};
    closure int() f = int() { return a[0]; };
    return f() != 0;
}
#elif defined block_type
int main(void)
{
    struct Local { int x; };
    struct Local v = {1};
    closure int() f = int() { return v.x; };
    return f() != 1;
}
#else
int main(void)
{
    int n = 1;
    int a[2] = {1, 2};
    int[:] s = a;
    closure int() f = int() { return n + s[0]; };
    int r = f();
    f = NULL;
    return r != 2;
}
#endif
