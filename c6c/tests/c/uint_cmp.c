/* expect: 42 */
/* 无符号关系比较必须用无符号条件码。用有符号的 jle 时
 * u64 0xcbf2... <= 0xFFFFFFFF 会恒为真（曾经把 64 位字面量截成 32 位）。 */
typedef unsigned long u64;
typedef unsigned int u32;

int main(void) {
    u64 big = 0xcbf29ce484222325UL;
    u64 small = 7;
    u32 m = 0xFFFFFFFFu;
    int n = 0;

    if (big <= 0xFFFFFFFFUL)
        return 1;
    if (big <= m)
        return 2;
    if (big < m)
        return 3;
    if (small > m)
        return 4;
    if (!(small <= m))
        return 5;
    if ((u32)small >= 8)
        return 6;
    n = (int)(big >> 32);
    if (n != 0xcbf29ce4)
        return 7;
    if ((u32)big != 0x84222325u)
        return 8;
    return 42;
}
