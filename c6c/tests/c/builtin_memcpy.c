/* args: -nostdlib */
/* compile-only */
/* `__builtin_memcpy` 是**内建**，不需要先有 memcpy 的声明。
 *
 * c6c 以前在预定义宏表里写 `#define __builtin_memcpy(a,b,c) memcpy(a,b,c)`，
 * 把它文本替换成一次普通调用——于是 TU 里没包含 <string.h> 的地方就报
 * "未声明的标识符 'memcpy'"。内核 include/vdso/unaligned.h:37 的
 * get_unaligned() 正是这种情形：它在 GNU 语句表达式里直接用
 * __builtin_memcpy，而外层文件不一定先声明过 memcpy
 * （实测 lib/xxhash.c、lib/hexdump.c、lib/test_rhashtable.c 各 3 条）。
 *
 * 这个用例刻意**不**包含 <string.h>、并且用 -nostdlib 编译（内核模式自己
 * 就关了运行时）：只要实现走内建就编得过。
 */
int probe(void)
{
	char dst[8] = { 0 };
	const char *src = "abc";
	unsigned char bytes[4] = { 1, 2, 3, 4 };
	unsigned int word = 0;

	__builtin_memcpy(dst, src, 4);
	if (dst[0] != 'a' || dst[1] != 'b' || dst[2] != 'c' || dst[3] != 0)
		return 1;
	/* 非字面量源、非恒定长度 */
	__builtin_memcpy(bytes, src, 3);
	if (bytes[0] != 'a' || bytes[1] != 'b' || bytes[2] != 'c' || bytes[3] != 4)
		return 2;
	__builtin_memcpy(&word, "ABCD", 4);
	if (word != 0x44434241u)
		return 3;
	return 42;
}
