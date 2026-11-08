/* c6 编译期诊断：强引用环（引用计数 GC，阶段 P6）
 *
 * 运行（仓库根目录）：
 *   tests/run-tests.sh errors/gc_cycle
 *
 * 引用计数回收不了环：环里的对象互相持有，谁都不会归零。所以在类型图上
 * （节点 = 结构体，边 = 强引用字段，含数组和按值嵌入；`weak`/`own` 不是边）
 * 出现自环或多节点环就是**编译错误**。两个出路：把某条边改成 `weak`
 * （反向引用，不持有）或 `own`（独占链/子树，见 tests/gc_own.c）。
 *
 * 不完整类型（只前向声明、别处定义）的边在这里不判定 —— 单 TU 只看得到
 * 本单元定义完整的结构体。
 */
struct Node { int v; };
struct A;
struct B;

// EXPECT cycle_self: error: reference cycle: Node2 -next-> Node2; break it with 'weak' or 'own'
// EXPECT cycle_mutual: error: reference cycle: A -b-> B -a-> A
// EXPECT cycle_array: error: reference cycle: Node3 -kids-> Node3
// EXPECT cycle_embed: error: reference cycle: Emb -w-> Wrap -e-> Emb

#if defined cycle_self
struct Node2 { @Node2 next; };
int main(void)
{
    return 0;
}
#elif defined cycle_mutual
struct A { @B b; };
struct B { @A a; };
int main(void)
{
    return 0;
}
#elif defined cycle_array
struct Node3 { @Node3 kids[4]; };
int main(void)
{
    return 0;
}
#elif defined cycle_embed
struct Wrap;
struct Emb { @Wrap w; };
struct Wrap { struct Emb e; };
int main(void)
{
    return 0;
}
#else
/* 正常路径：weak 反向引用、own 独占链、无环的互递归（DAG）都合法 */
struct Node4 { int v; weak @Node4 parent; own @Node4 child; };
struct Owner { int v; own @Owner next; };
struct P;
struct Q;
struct P { @Q q; };
struct Q { weak @P p; };

int main(void)
{
    unsigned long base = __c6_gc_live();
    {
        @Node4 root = new Node4;
        @Node4 kid = new Node4;

        root->v = 1;
        kid->v = 2;
        root->child = move(kid);        /* own：独占子树 */
        kid = new Node4;
        kid->parent = root;             /* weak：不持有 */
        {
            @Node4 up = kid->parent;    /* weak 要先升级才能解引用 */
            if (up->v != 1)
                return 1;
        }
        if (__c6_gc_live() != base + 3) /* root + 子树里的 kid + 新的 kid */
            return 2;
    }
    return __c6_gc_live() == base ? 0 : 3;
}
#endif
