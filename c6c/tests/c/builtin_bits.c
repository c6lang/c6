/* expect: 42 */
/* `__builtin_{clz,ctz,ffs,popcount}{,l,ll}`：内核 arch/x86/include/asm/
 * bitops.h 与 include/linux/hweight.h 的基础件。
 *
 * 宽度按**实参类型**定（clz(int) 至多 32，clzl(long) 至多 64）；`ffs` 是
 * 1 基、输入 0 给 0。这里还连带钉住了 `1UL` 的宽度——`clzl(1UL<<40)` 要是
 * 按 32 位算，移位后是 0，答案会是 64 而不是 40。
 */
int printf(const char *fmt, ...);

static int failures;

static void ck(long got, long want, const char *what)
{
    if (got != want) {
        printf("FAIL %s: got %ld want %ld\n", what, got, want);
        failures++;
    }
}

int main(void)
{
    ck(__builtin_clz(1), 31, "clz(1)");
    ck(__builtin_clz(0x80000000u), 0, "clz(0x80000000)");
    ck(__builtin_clzl(1UL), 63, "clzl(1)");
    ck(__builtin_ctz(8), 3, "ctz(8)");
    ck(__builtin_ctz(1), 0, "ctz(1)");
    ck(__builtin_ctzl(1UL << 40), 40, "ctzl(1UL<<40)");
    ck(__builtin_ffs(0), 0, "ffs(0)");
    ck(__builtin_ffs(1), 1, "ffs(1)");
    ck(__builtin_ffs(8), 4, "ffs(8)");
    ck(__builtin_ffsl(1L << 40), 41, "ffsl(1L<<40)");
    ck(__builtin_popcount(0), 0, "popcount(0)");
    ck(__builtin_popcount(7), 3, "popcount(7)");
    ck(__builtin_popcount(0xFF), 8, "popcount(0xFF)");
    ck(__builtin_popcountll(0xFFFFFFFFFFLL), 40, "popcountll");
    return failures ? 1 : 42;
}
