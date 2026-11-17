/* expect: 42 */
/* 内核 include/linux/compiler.h 的 __is_constexpr()：
 *     sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8))
 * 靠"值为 0 的整型**常量表达式**是空指针常量"（C11 6.3.2.3p3）区分编译期
 * 常量和运行期值——常量时三元式取 int* 那半，sizeof 相等；非常量时那半是
 * void*，sizeof 不等。overflow.h 的 struct_size()/DEFINE_FLEX() 整条靠它选
 * 编译期分支，只认字面量 0 的话内核那几个文件直接编不过。 */
#define __is_constexpr(x) \
	(sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))

#define ICE(x) __is_constexpr(x)

/* DEFINE_FLEX 的形状：数组维度上要一个编译期常量 */
#define FLEX_BYTES(type, member, count, extra) \
	(sizeof(type) + (count) * sizeof(((type *)0)->member[0]) + (extra))

struct info {
	int a;
	int flex[];
};

#define DEFINE_FLEX(type, name, member, count) \
	_Static_assert(__builtin_constant_p(count), "const count"); \
	union { unsigned char bytes[FLEX_BYTES(type, member, count, 0)]; type obj; } name##_u; \
	type *name = (type *)&name##_u

static int runtime_n = 3;

int main(void)
{
	int arr1[__is_constexpr(16 + 2) ? 4 : 9];
	int arr2[__is_constexpr(runtime_n) ? 4 : 9];
	/* 非常量的 0 值不是空指针常量 */
	int arr3[__is_constexpr(runtime_n * 0) ? 4 : 9];
	int arr4[(sizeof(int) == sizeof(*(8 ? (void *)0 : (int *)8))) ? 4 : 9];
	DEFINE_FLEX(struct info, e4b, flex, 16 + 2);
	int r = 0;

	if (sizeof(arr1) != 4 * sizeof(int))
		r += 1;               /* 常量：判 1 → 4 */
	if (sizeof(arr2) != 9 * sizeof(int))
		r += 2;               /* 运行期值：判 0 → 9 */
	if (sizeof(arr3) != 9 * sizeof(int))
		r += 4;               /* 非常量的 0：仍不是「常量表达式」 */
	if (sizeof(arr4) != 4 * sizeof(int))
		r += 8;               /* 字面量 0 一直是空指针常量 */
	if (sizeof(e4b_u) < sizeof(struct info) + 18 * sizeof(int))
		r += 16;
	return r == 0 ? 42 : r;
}
