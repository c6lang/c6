/* expect: 42 */
/* 花括号里直接一个字符串字面量填**本层**数组：`char m[8] = { "abc" };`。
 * init_sync 只往子对象里钻（数组元素是标量，钻不下去），层栈里没有数组可
 * 找，于是落到 const_expr 报"这里要一个常量表达式"。内核 fs/coredump.c 的
 * core_modes[]（相邻字符串拼接）和 fs/proc/task_mmu.c 的
 *     static const char mnemonics[BITS_PER_LONG][3] = {
 *         [0 ... (BITS_PER_LONG-1)] = "??", …
 * 都是这个形状，后者还叠着"区间设计符 + 字符串"。 */
static char whole[8] = { "abc" };
static char unsized[] = { "abc" };
static char joined[] = {
	"file\npipe"
	"\nsocket"
};
static const char mnemonics[4][3] = {
	[0 ... 3] = "??",
	[1] = "rd",
};

int main(void)
{
	char local[8] = { "abc" };
	const char ltab[4][3] = { [0 ... 3] = "??" };

	if (whole[2] != 'c' || whole[3] != 0)
		return 1;
	if (unsized[0] != 'a')
		return 2;
	if (joined[0] != 'f' || joined[4] != '\n' || joined[5] != 'p'
	    || joined[10] != 's')
		return 3;
	if (mnemonics[0][0] != '?' || mnemonics[1][0] != 'r' || mnemonics[3][1] != '?')
		return 4;
	if (local[1] != 'b' || local[4] != 0)
		return 5;
	if (ltab[2][1] != '?')
		return 6;
	return 42;
}
