/* expect: 42 */
/* 64 位字面量：超过 32 位的常量必须整条进函数常量池，不能被 imm32 截断。 */
typedef unsigned long u64;
typedef long i64;

static u64 g64 = 0xcbf29ce484222325UL;
static i64 neg = -1;

int main(void) {
    u64 v = 0x0123456789ABCDEFUL;
    u64 s = 0;

    if (g64 != 0xcbf29ce484222325UL)
        return 1;
    if ((u64)neg != 0xFFFFFFFFFFFFFFFFUL)
        return 2;
    s = g64 + v;
    if (s != 0xccf5730dd906e1f4UL)
        return 3;
    if ((v & 0xFFFFFFFFUL) != 0x89ABCDEFUL)
        return 4;
    if ((v >> 32) != 0x01234567UL)
        return 5;
    return 42;
}
