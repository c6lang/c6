/* expect: 42 */
/* 常量表达式里的比较要走 C 的"寻常算术转换"（6.3.1.8），不能拿 i64 裸比。
 * 内核 include/linux/overflow.h:33
 *     #define is_signed_type(type) (((type)(-1)) < (type)1)
 * 判错会让 type_max() 只剩一半宽，arch/x86/include/asm/svm.h 的
 * static_assert((X2AVIC_4K_MAX_PHYSICAL_ID & AVIC_PHYSICAL_MAX_INDEX_MASK) == …)
 * 全假失败。 */
#define is_signed_type(type) (((type)(-1)) < (type)1)
#define half_max(type) ((type)1 << (8 * sizeof(type) - 1 - is_signed_type(type)))
#define type_max(type) ((type)((half_max(type) - 1) + half_max(type)))

_Static_assert(!is_signed_type(unsigned long long), "ull 无符号");
_Static_assert(is_signed_type(long long), "ll 有符号");
_Static_assert(!is_signed_type(unsigned char), "uchar 提升成 int 后仍无符号口径");
_Static_assert(type_max(unsigned long long) == 0xFFFFFFFFFFFFFFFFULL, "ull 满宽");
_Static_assert(type_max(unsigned int) == 0xFFFFFFFFU, "uint 满宽");
_Static_assert(type_max(unsigned char) == 0xFFU, "uchar 满宽");
_Static_assert(type_max(int) == 0x7FFFFFFF, "int 满宽");

int main(void)
{
	int r = 0;

	/* 同宽有符号 vs 无符号：无符号胜出 */
	if (!((int)-1 < 1u))
		r += 1;
	/* 宽的有符号能装下窄的无符号：仍有符号 */
	if ((long)-1 < 1u)
		r += 2;
	/* 64 位无符号：0xFFFF…FFFF 不小于 1 */
	if (!((0ULL - 1) < 1ULL))
		r += 4;
	if (((0ULL - 1) == 0xFFFFFFFFFFFFFFFFULL))
		r += 8;
	if (((unsigned long long)-1) > 0)
		r += 16;
	/* 窄类型提升成 int：0xFF 不是 -1 */
	if ((unsigned char)0xFF == 255)
		r += 11;
	return r;
}
