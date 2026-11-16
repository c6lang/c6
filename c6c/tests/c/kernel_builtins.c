/* expect: 42 */
/* 内核头链上用到的几个内建函数。
 *
 *   __builtin_va_copy        include/linux/stdarg.h
 *   __builtin_prefetch       include/linux/prefetch.h
 *   __builtin_extract_return_addr   lib/vsprintf.c 的 %pS
 *   __builtin_bswap16/32/64  include/uapi/linux/swab.h（常量层也要有）
 */
#include <stdio.h>
#include <stdarg.h>

static int sum_copy(int n, ...)
{
	va_list ap, ap2;
	int s = 0;

	va_start(ap, n);
	va_copy(ap2, ap);
	while (n-- > 0)
		s += va_arg(ap2, int);
	va_end(ap2);
	va_end(ap);
	return s;
}

int main(void)
{
	int x = 41;
	int *p = &x;
	unsigned long addr = (unsigned long)p;

	/* prefetch 是纯提示，值不参与运算，但实参副作用要保留 */
	__builtin_prefetch(p);
	if (*p != 41)
		return 1;
	/* extract_return_addr 在 x86-64 上是恒等 */
	if ((unsigned long)__builtin_extract_return_addr((void *)addr) != addr)
		return 2;
	if (__builtin_bswap16(0x1234) != 0x3412)
		return 3;
	if (__builtin_bswap32(0x12345678u) != 0x78563412u)
		return 4;
	if (__builtin_bswap64(0x0102030405060708ULL) != 0x0807060504030201ULL)
		return 5;
	if (sum_copy(3, 10, 20, 12) != 42)
		return 6;
	return 42;
}
