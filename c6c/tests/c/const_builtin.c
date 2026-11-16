/* expect: 42 */
/* 常量求值层的内建与整型提升。
 *
 * 内核里 ilog2()/bits_per() 这类宏整条都要能在 _Static_assert、数组长度
 * 里求值；路径上踩过的坑都钉在这里：
 *   - `~0U` 是 32 位的 0xFFFFFFFF，不是 64 位的 0xFFFF…FF；
 *   - `(T)x` 在常量层要真按 T 截断；
 *   - 枚举措名出现在括号里时括号不是强制转换；
 *   - 三元的未选中支可以有函数调用。 */
#include <stdio.h>

#define BIT(n) (1UL << (n))
#define ilog2(n) \
	(__builtin_constant_p(n) ? \
	 ((n) < 2 ? 0 : 63 - __builtin_clzll(n)) : \
	 __ilog2_u64(n))

static int __ilog2_u64(unsigned long n)
{
	int r = 0;

	while (n > 1) {
		n >>= 1;
		r++;
	}
	return r;
}

enum { CT_STATE_MAX = 5 };

/* 常量上下文：数组长度与静态断言 */
static const unsigned long u32max = (unsigned long)(~0U);
static int arr[ilog2(16) + 1];

_Static_assert((unsigned long)(~0U) == 4294967295UL, "~0U is 32-bit");
_Static_assert((unsigned long)(~0U) <= 0xFFFFFFFFUL, "u32 max");
_Static_assert(ilog2(16) == 4, "ilog2(16)");
_Static_assert(ilog2(1) == 0, "ilog2(1)");
_Static_assert(ilog2(1024) == 10, "ilog2(1024)");
_Static_assert(__builtin_constant_p(5), "literal is constant");
_Static_assert(__builtin_clzll(1UL) == 63, "clzll(1)");
_Static_assert(__builtin_clzll(1UL << 40) == 23, "clzll(1<<40)");
_Static_assert(__builtin_popcount(0xF0u) == 4, "popcount");
_Static_assert(__builtin_ctz(8u) == 3, "ctz(8)");
_Static_assert(__builtin_ffs(8u) == 4, "ffs(8)");
/* 枚举措名在括号里：括号不是 cast */
_Static_assert((CT_STATE_MAX - 1) == 4, "enum paren");

int main(void)
{
	int v = 8;

	if (u32max != 4294967295UL)
		return 1;
	if (sizeof(arr) / sizeof(arr[0]) != 5)
		return 2;
	if (ilog2(16) != 4 || ilog2(1) != 0 || ilog2(1024) != 10)
		return 3;
	/* 非常量走运行期分支 */
	if (ilog2(v) != 3)
		return 4;
	if (__builtin_constant_p(v))
		return 5;
	return 42;
}
