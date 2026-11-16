/* expect: 42 */
/* `__builtin_{add,sub,mul}_overflow(a, b, &out)`：内核
 * include/linux/overflow.h 的整族 check_*_overflow() 都压在这三个上。
 *
 * 这里把两个语义点都钉住：
 *   - 结果要**写回** `*out`（不写回，下面 r 的六个读数全是垃圾）
 *   - 溢出判据要在**目标类型的宽度**上做。只在 64 位上判符号的话，
 *     `int 2147483647 + 1` 在 64 位里算得下，六个 ov 会全是 0。
 */
int printf(const char *fmt, ...);

static int failures;

static void ck(int cond, const char *what)
{
    if (!cond) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

int main(void)
{
    int r;
    unsigned ur;
    int ov;

    ov = __builtin_add_overflow(2147483647, 1, &r);
    ck(r == -2147483648 && ov == 1, "signed add overflow");
    ov = __builtin_add_overflow(2, 3, &r);
    ck(r == 5 && ov == 0, "signed add ok");

    ov = __builtin_sub_overflow(-2147483647 - 1, 1, &r);
    ck(r == 2147483647 && ov == 1, "signed sub overflow");
    ov = __builtin_sub_overflow(10, 3, &r);
    ck(r == 7 && ov == 0, "signed sub ok");

    ov = __builtin_mul_overflow(65536, 65536, &r);
    ck(r == 0 && ov == 1, "signed mul overflow");
    ov = __builtin_mul_overflow(6, 7, &r);
    ck(r == 42 && ov == 0, "signed mul ok");

    ov = __builtin_add_overflow(0xFFFFFFFFu, 1u, &ur);
    ck(ur == 0 && ov == 1, "unsigned add overflow");

    return failures ? 1 : 42;
}
