/* C99 6.5.2 后缀运算符作用于**任意**后缀表达式，以及 6.5.2.1p2 的下标交换律：
 *   - `(struct S){1,2}.y`、`(int[]){7,8,9}[1]`：复合字面量后面直接跟 `.`/`[`；
 *   - `1[a]`、`i[a]` 和 `a[i]` 等价（`E1[E2]` 就是 `*(E1+E2)`）。
 * 这两条以前都被拒（"这里要 ')'" / "下标访问的对象不是数组也不是指针"）。 */
#include <stdio.h>

struct S { int x; int y; };
struct Big { int a[3]; struct S s; };

int main(void)
{
	int a[3] = {1, 2, 3};
	int i = 1;
	struct S s = (struct S){5, 6};
	int bad = 0;

	if ((struct S){5, 6}.y != 6)
		bad++;
	if ((int[]){7, 8, 9}[1] != 8)
		bad++;
	if ((struct Big){.a = {1, 2, 3}, .s = {4, 5}}.s.y != 5)
		bad++;
	if ((int[]){10, 20}[0] != 10)
		bad++;
	if (a[i] != i[a] || i[a] != 1[a] || 2[a] != 3)
		bad++;
	if (s.x != 5 || s.y != 6)
		bad++;
	/* 复合字面量仍然是左值：能取地址、能改 */
	{
		struct S *p = &(struct S){7, 8};

		p->x = 9;
		if (p->x != 9 || p->y != 8)
			bad++;
	}
	printf("postfix ok=%d bad=%d\n", bad == 0, bad);
	return bad != 0;
}
