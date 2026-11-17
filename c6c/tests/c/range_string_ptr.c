/* expect: 42 */
/* 区间设计符的值是指针时，字符串字面量给的是它的**地址**，区间里每个元素
 * 都指向同一份数据：
 *     static const char * const smca_names[] = {
 *         [SMCA_CS ... SMCA_CS_V2] = "coherent_station",
 *     };
 * 内核 arch/x86/kernel/cpu/mce/amd.c 就是这么写的。元素是 char 数组时字符串
 * 填的是内容（见 braced_string_array.c），这里补指针那一半，局部和全局都走。 */
static const char *const global_tab[] = {
	[0 ... 3] = "wide",
	[2] = "narrow",
};

int main(void)
{
	const char *local_tab[3] = { [0 ... 2] = "ab" };

	if (global_tab[0][0] != 'w' || global_tab[3][2] != 'd')
		return 1;
	if (global_tab[2][0] != 'n')
		return 2;
	if (local_tab[0][0] != 'a' || local_tab[2][1] != 'b')
		return 3;
	if (global_tab[1] != global_tab[3])
		return 4;
	return 42;
}
