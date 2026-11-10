/* c6 闭包类型与签名的诊断（C90）
 *
 * `closure` 后面必须是一个**完整**的函数签名：返回类型、参数表和括号都在。
 * 签名里的类型必须能在文件作用域写回来（匿名 struct/union/enum、块作用域
 * 的 tag 都不行），否则字面量体在翻译单元末尾才编译时就没法重建签名。
 * 闭包本身就是托管引用，所以不能再套一层 '@'。
 */

// EXPECT need_fullsig: error: a 'closure' type needs a full signature: 'closure RET (parameters)'
// EXPECT anon: error: a closure signature cannot use an anonymous 'struct'/'union'/'enum': give it a tag
// EXPECT block_tag: error: a closure signature cannot use a type declared in a block
// EXPECT at_closure: error: '@' cannot be applied to a closure type: a closure is already a managed reference

#if defined need_fullsig
int main(void)
{
    closure int f = 0;
    return f != NULL;
}
#elif defined anon
int main(void)
{
    closure int(struct { int x; })(int) f = 0;
    return f != NULL;
}
#elif defined block_tag
int main(void)
{
    struct Local { int x; };
    closure int(struct Local)(int) f = 0;
    return f != NULL;
}
#elif defined at_closure
int main(void)
{
    int n = 1;
    closure int() f = int() { return n; };
    @closure int() g = f;
    return g != NULL;
}
#else
int main(void)
{
    int n = 1;
    closure int() f = int() { return n; };
    int r = f();
    f = NULL;
    return r != 1;
}
#endif
