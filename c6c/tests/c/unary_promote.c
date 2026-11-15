/* C99 一元 `+` 的整型提升（6.5.3.3p2）：`+E` 的结果是 E 提升后的类型，
 * 所以 `sizeof(+c)`（c 是 signed char）是 4 而不是 1；`_Bool` 也参与整型提升
 * （6.3.1.1p2）。`sizeof(-c)`、`c + 0` 两条路本来就有提升，只有一元 `+` 漏了。
 *
 * 顺带钉住：`sizeof(ch)` 仍是 1（没有提升），`sizeof(+1.5f)` 是 4（浮点不提升），
 * `sizeof(+10L)` 是 8（long 不缩），`sizeof(+'a')` 是 4（字符常量本来就是 int）。 */
#include <stdio.h>

int main(void)
{
	signed char c = -1;
	short s = -1;
	unsigned char uc = 200;
	char ch = 'A';
	_Bool b = 1;
	int bad = 0;

	if ((int)sizeof(+c) != 4)
		bad++;
	if ((int)sizeof(+s) != 4)
		bad++;
	if ((int)sizeof(+uc) != 4)
		bad++;
	if ((int)sizeof(+ch) != 4)
		bad++;
	if ((int)sizeof(+b) != 4)
		bad++;
	if ((int)sizeof(-c) != 4)
		bad++;
	if ((int)sizeof(c + 0) != 4)
		bad++;
	if ((int)sizeof(c) != 1)
		bad++;
	if ((int)sizeof(+10L) != 8)
		bad++;
	if ((int)sizeof(+1.5f) != 4)
		bad++;
	if ((int)sizeof(+(char)1) != 4)
		bad++;
	if ((int)sizeof(+'a') != 4)
		bad++;
	/* 值本身也不能变：提升之后 -1 仍是 -1 */
	if (+c != -1 || +s != -1 || +uc != 200)
		bad++;
	printf("unary ok=%d bad=%d %d %d %d\n", bad == 0, bad, +c, +s, +uc);
	return bad != 0;
}
