/* C99 初始化器的边角（6.7.8）：花括号省略（嵌套聚合不给内层花括号）、
 * 多维数组、指定初始化器（`[i]`、`.成员`、混用、重复覆盖、嵌套）、部分初始化
 * 后其余清零、整数后缀、多字符常量、结构体数组的元素设计符。
 *
 * 输出要和 gcc -std=c99 逐字节一致（三后端也一样）。 */
#include <stdio.h>
#include <string.h>

struct P { int x, y; };
struct R { struct P a, b; int z; };

int main(void)
{
	/* 花括号省略：嵌套聚合可以不给内层花括号 */
	int a[2][2] = {1, 2, 3, 4};
	struct R r = {1, 2, 3, 4, 5};
	struct P ps[2] = {1, 2, 3, 4};
	int m[2][2][2] = {1, 2, 3, 4, 5, 6, 7, 8};
	char cs[3][4] = {"ab", "cd", "ef"};
	/* 部分初始化 + 其余清零 */
	int part[5] = {1, 2};
	/* 指定初始化器混用、乱序、重复覆盖（后写的生效） */
	int mix[6] = {1, [3] = 4, 5};
	struct P ov = {.x = 1, .y = 2, .x = 3};
	/* 整数后缀 */
	unsigned u1 = 10u, u2 = 10U;
	long l1 = 10l, l2 = 10L;
	unsigned long long ull = 10ull;
	long long ll2 = 10LL;
	/* 字符/字符串 */
	int ch = 'ab';                /* 多字符常量：实现定义，gcc 是 0x6162 */
	char s[] = "hi";

	printf("a=%d %d %d %d\n", a[0][0], a[0][1], a[1][0], a[1][1]);
	printf("r=%d %d %d %d %d\n", r.a.x, r.a.y, r.b.x, r.b.y, r.z);
	printf("ps=%d %d %d %d\n", ps[0].x, ps[0].y, ps[1].x, ps[1].y);
	printf("m=%d\n", m[1][1][1]);
	printf("cs=%s %s %s\n", cs[0], cs[1], cs[2]);
	printf("part=%d %d\n", part[0], part[4]);
	printf("mix=%d %d %d %d\n", mix[0], mix[3], mix[4], mix[5]);
	printf("ov=%d %d\n", ov.x, ov.y);
	printf("suffix=%u %u %ld %ld %llu %lld\n", u1, u2, l1, l2, ull, ll2);
	printf("ch=%d s=%s sizeof=%d\n", ch, s, (int)sizeof("hi"));
	/* 结构体数组的元素设计符 + 嵌套设计符 */
	{
		struct P q[2] = {[1] = {.y = 9}};
		struct R rr = {.a = {.x = 7}, .b = {8, 9}, .z = 10};

		printf("q=%d %d %d %d\n", q[1].x, q[1].y, q[0].x, q[0].y);
		printf("rr=%d %d %d %d %d\n", rr.a.x, rr.a.y, rr.b.x, rr.b.y, rr.z);
	}
	/* 二维数组的行设计符 + 行内元素设计符 */
	{
		int t[3][2] = {[1] = {5, 6}, [2][1] = 9};

		printf("t=%d %d %d %d %d %d\n", t[0][0], t[0][1], t[1][0], t[1][1],
		       t[2][0], t[2][1]);
	}
	return 0;
}
