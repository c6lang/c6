/* 联合体的初始化（C99 6.7.8）：
 *   - 不带设计符时只初始化第一个成员；
 *   - 带设计符（`.f = 1.0f`）要按**那个成员的类型**落值。
 *
 * 第二条踩过：叶表给联合体只登记第一个成员，于是 `union U a = { .f = 1.0f };`
 * 被按 `int` 存成 1，读 `a.f` 是 5.6e-45 这种垃圾（全局那条路以前是对的，
 * 只有自动存储期错）。含 union 的 struct 里同样如此。 */
#include <stdio.h>

union U { int i; float f; double d; char b[8]; };
struct S { int tag; union U u; };

static union U g_u = { .f = 3.0f };            /* 全局：一直是好的 */

int main(void)
{
	float one = 1.0f;
	double half = 0.5;
	union U a = { .f = 1.0f };
	union U b = { .f = one };
	union U c = { .d = half };
	union U e = { 42 };                    /* 不带设计符：第一个成员 int */
	struct S s = { .u.f = 2.0f, .tag = 7 };
	int bad = 0;

	if (a.i != 1065353216)                 /* 1.0f 的位模式 */
		bad++;
	if (b.i != 1065353216)
		bad++;
	if (c.d != 0.5)
		bad++;
	if (e.i != 42)
		bad++;
	if (s.u.f != 2.0f || s.tag != 7)
		bad++;
	if (g_u.f != 3.0f)
		bad++;
	printf("union ok=%d bad=%d\n", bad == 0, bad);
	return bad != 0;
}
