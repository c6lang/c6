/* C99 变参（7.15）：三后端要一致。
 *
 * 这条用例专门盯 LLVM 后端以前的老毛病：`printf("%d", 7)` 印 0。原因是它
 * 把 va_start/va_arg 都落在"函数入口自己的 %ap"上，前端那个 `va_list`
 * 对象根本没被初始化，于是"把 va_list 传给别的函数"（`printf` → `vfprintf`）
 * 必然读到垃圾。现在两处都改成用前端对象（`__c6c_va_elem[1]`，24 字节，
 * 形状就是 SysV 的），并且允许非变参函数取传进来的 va_list。
 *
 * 输出要和 native 后端逐字节一致（tests/c99-diff.sh、llvm-diff.sh 比的是
 * 标准输出 + 退出码），所以这里尽管打。 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int isum(int n, ...)
{
	va_list ap;
	int t = 0, i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		t += va_arg(ap, int);
	va_end(ap);
	return t;
}

static double dsum(int n, ...)
{
	va_list ap;
	double t = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		t += va_arg(ap, double);
	va_end(ap);
	return t;
}

/* 把 va_list 往下传：这正是 printf 的做法 */
static int take(va_list ap, int n)
{
	int t = 0, i;

	for (i = 0; i < n; i++)
		t += va_arg(ap, int);
	return t;
}

static int fwd(int n, ...)
{
	va_list ap;
	int r;

	va_start(ap, n);
	r = take(ap, n);
	va_end(ap);
	return r;
}

static int mixed(const char *fmt, ...)
{
	va_list ap, cp;
	char buf[64];
	int n;

	va_start(ap, fmt);
	va_copy(cp, ap);
	n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (strcmp(buf, "7-3.5-xy") != 0)
		n = -1;
	/* va_copy 出来的那份还能用一次 */
	if (va_arg(cp, int) != 7)
		n = -2;
	va_end(cp);
	return n;
}

int main(void)
{
	printf("ints=%d %d %d\n", 1, -2, 300);
	printf("mix=%d %g %s %c\n", 42, 3.5, "hi", 'Z');
	printf("wide=%llu %lld\n", 18446744073709551615ULL, -1234567890123LL);
	printf("hex=%x %o %u\n", 255, 8, 4000000000u);
	printf("pad=[%5d][%-5d][%05d]\n", 42, 42, 42);
	printf("prec=[%.3f][%.2s]\n", 3.14159, "abcdef");
	printf("isum=%d dsum=%g fwd=%d\n", isum(3, 1, 2, 3), dsum(2, 0.5, 0.25), fwd(3, 4, 5, 6));
	printf("mixed=%d\n", mixed("%d-%g-%s", 7, 3.5, "xy"));
	return 0;
}
