/* C99 常用算术转换（6.3.1.8）与除法的有符号性：三个后端要一致。
 *
 * 这条用例盯两个当年踩过的坑：
 *   1) `long / unsigned int`：unsigned int 能装进 long，所以**先转成 long**，
 *      结果是有符号除法。以前按"任一操作数无符号就无符号"判，`long l = -2;
 *      unsigned u = 2; l / u` 算成 9223372036854775807（`%` 同理）。
 *   2) 混合符号比较也要按公共类型来（`-1L < 1u` 是 1，`-1 < 1u` 是 0）。
 *
 * 判对错只看 stdout（脚本按三后端逐字节比），所以直接打印。 */
#include <stdio.h>

int main(void)
{
	long l = -2;
	long l3 = -3;
	unsigned u = 2;
	unsigned int ui = 2;
	int i = 2;
	unsigned long long ull = 3;
	unsigned long long big = 18446744073709551615ULL;
	short s = -7;
	unsigned short us = 7;
	signed char sc = -1;

	printf("div=%ld %ld %ld\n", l / u, l / ui, l3 / u);
	printf("mod=%ld %ld\n", l % u, l3 % u);
	printf("same=%ld %ld\n", l / i, l3 % i);
	printf("cmp=%d %d %d %d\n", -1 < 1u, -1L < 1u, l < u, -1LL < 1u);
	printf("promote=%d %ld %ld\n", s + us, (long)(s * 2), (long)(us / 2));
	printf("u64=%llu %llu %llu\n", big / ull, big % ull, big >> 1);
	printf("narrow=%d %d\n", (int)(sc / 2), (int)(sc % 2));
	printf("shift=%d %d\n", -8 >> 1, (int)((unsigned)-8 >> 28));
	return 0;
}
