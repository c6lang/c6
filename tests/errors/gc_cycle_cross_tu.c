/* c6 编译期诊断：跨翻译单元的强引用环（引用计数 GC，阶段 P6）
 *
 * 运行（仓库根目录）：
 *   tests/run-tests.sh errors/gc_cycle_cross_tu
 *
 * 单 TU 只能看本单元定义完整的类型：`@B` 里 B 只有前向声明时，那条边只
 * 记录下来、不下结论。每个 TU 把边写进 `.c6.gcgraph` 段（类型名哈希），
 * 链接时同名段合并后跑一次 DFS —— 于是「A 在 TU1、B 在 TU2、互相强引用」
 * 这种漏报被堵住。
 *
 * 第二半在同名的 tests/tu/gc_cycle_cross_tu.c 里。
 */
struct B;

// EXPECT cycle_cross: error: reference cycle across translation units: A -> B -> A
// NATIVE-ONLY cycle_cross   （C99 后端不链接，见 docs/gc-design.md §15）

#if defined cycle_cross
/* A 强引用 B（B 在另一个 TU 里强引用 A）→ 链接期成环 */
struct A { int v; @B b; };

int main(void)
{
    return 0;
}
#else
/* 正常路径：A 不指向 B，只有 B→A 一条边，不成环 */
struct A { int v; };

int tu_b_ok(void);

int main(void)
{
    return tu_b_ok() ? 0 : 1;
}
#endif
