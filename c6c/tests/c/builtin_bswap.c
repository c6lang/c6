/* expect: 42 */
/* `__builtin_bswap16/32/64`：内核 include/uapi/linux/swab.h 的
 * __swab16/32/64 直接压在它们上，进而整个字节序转换链。 */
int printf(const char *fmt, ...);

static int failures;

static void ck(unsigned long got, unsigned long want, const char *what)
{
    if (got != want) {
        printf("FAIL %s: got 0x%lx want 0x%lx\n", what, got, want);
        failures++;
    }
}

int main(void)
{
    ck(__builtin_bswap16(0x1234), 0x3412UL, "bswap16");
    ck(__builtin_bswap32(0x12345678u), 0x78563412UL, "bswap32");
    ck(__builtin_bswap64(0x0102030405060708UL), 0x0807060504030201UL, "bswap64");
    return failures ? 1 : 42;
}
