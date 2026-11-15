/* C99 变长数组形参（6.7.5.3p7）：`void f(int n, int m, int a[n][m])`。
 *
 * 形参表里 n/m 还没有值、槽也还没建，所以行 stride 不能在解析形参表时算。
 * 做法是在声明符上留一份"每一维是常量还是哪个形参名"的形状，等函数入口按
 * 形参槽里的值现算 stride，存进那个形参符号——于是 `a[i][j]` 直接走块作用域
 * 多维 VLA 的下标逻辑，三个后端一行都不用改。
 *
 * 明确**不支持**、会报错的：指向变长数组的指针 `int (*p)[m]`（运行期 stride
 * 没有地方放，与其算错不如说清）。 */
#include <stdio.h>

/* 二维 VLA 形参：内维是形参名 */
static int sum2(int n, int m, int a[n][m])
{
	int i, j, t = 0;

	for (i = 0; i < n; i++)
		for (j = 0; j < m; j++)
			t += a[i][j];
	return t;
}

/* 内维是常量 */
static int sum2c(int n, int a[n][5])
{
	int i, j, t = 0;

	for (i = 0; i < n; i++)
		for (j = 0; j < 5; j++)
			t += a[i][j];
	return t;
}

/* 二维 VLA 形参里写元素、再读回来（验证 stride 同时用在下标和取地址上） */
static void fill(int n, int m, int a[n][m])
{
	int i, j;

	for (i = 0; i < n; i++)
		for (j = 0; j < m; j++)
			a[i][j] = i * m + j;
}

/* `sizeof a` 是**指针**大小（形参退化） */
static int psz2(int n, int m, int a[n][m])
{
	(void)a;
	(void)n;
	(void)m;
	return (int)sizeof a;
}

int main(void)
{
	int n = 2, m = 3, i, j, a[2][3];
	int b[2][5];
	int bad = 0;

	for (i = 0; i < n; i++)
		for (j = 0; j < m; j++)
			a[i][j] = i * 10 + j;
	for (i = 0; i < 2; i++)
		for (j = 0; j < 5; j++)
			b[i][j] = i + j;

	if (sum2(n, m, a) != 36)          /* (0+1+2) + (10+11+12) */
		bad++;
	if (sum2c(2, b) != 25)            /* (0+1+2+3+4) + (1+2+3+4+5) */
		bad++;
	fill(n, m, a);
	if (a[1][2] != 5 || a[0][1] != 1)
		bad++;
	if (psz2(n, m, a) != (int)sizeof(int *))
		bad++;
	printf("vla_param ok=%d bad=%d sum=%d sum2c=%d\n", bad == 0, bad,
	       sum2(n, m, a), sum2c(2, b));
	return bad != 0;
}
