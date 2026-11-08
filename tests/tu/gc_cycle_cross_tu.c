/* tests/errors/gc_cycle_cross_tu.c 的第二个翻译单元。
 *
 * B 强引用 A，A 只在另一半里定义：本单元看不到 A 的字段，所以单 TU 检查
 * 只能记一条边（A 在这里是不完整类型），结论留给链接期。默认那一路里
 * A 不指向 B，没有环；-Dcycle_cross 会把 A→B 补上，于是链接期看到
 * A→B→A。
 */
struct A;

struct B { int w; @A a; };

int tu_b_ok(void)
{
    return sizeof(struct B) > 0;      /* 只算大小，不实例化 A */
}
