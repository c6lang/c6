/* 字符常量与宽字符串字面量（C99 6.4.4.4 / 6.4.5）：
 *   - 单字符常量按 `char` 求值再转 int，c6c 的 char 是 signed，所以
 *     `'\377'` 是 -1（不是 255）；多字符常量是实现定义，`'ab'` 保持 0x6162；
 *   - `L"…"` 的元素是 4 字节的 wchar_t：sizeof 要按元素宽度算，
 *     `sizeof(L"abc")` 是 16 不是 4。（`U"…"` 是 C11 的，C99 不测。） */
#include <stdio.h>

int main(void)
{
	const int *w = (const int *)L"abc";
	int bad = 0;

	if ((int)'\377' != -1)
		bad++;
	if ((int)'\x80' != -128)
		bad++;
	if ((int)'\x7f' != 127)
		bad++;
	if ((int)'A' != 65)
		bad++;
	if ((int)'\n' != 10)
		bad++;
	if ('ab' != 0x6162)
		bad++;
	if ((int)sizeof(L"abc") != 16)
		bad++;
	if ((int)sizeof(L"") != 4)
		bad++;
	if ((int)sizeof("abc") != 4)
		bad++;
	if (w[1] != (int)'b')
		bad++;
	printf("charlit ok=%d bad=%d\n", bad == 0, bad);
	return bad != 0;
}
