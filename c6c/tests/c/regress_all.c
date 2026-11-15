/* 一整轮"外部扫描找出来的真缺陷"的合并回归：每一条当年都是**静默算错**
 * 或**合法 C99 被拒**，现在一起钉住（三后端 stdout 逐字节一致 + 和 gcc 一致）：
 *
 *   1) 有符号窄载入（char/short/int）必须符号扩展，否则 -1 变 4294967295
 *      （C99/LLVM 后端以前一律零扩展）；
 *   2) `long / unsigned int` 是**有符号**除法（6.3.1.8：unsigned int 能装进 long）；
 *   3) 全局/静态初始化器里的地址常量：`&A[0]`、`A + 2`、`&s.x`；
 *   4) 复合字面量后面直接跟 `.成员`/`[下标]`（6.5.2）；
 *   5) 下标交换律 `k[arr]`、`2[arr]`（6.5.2.1p2）；
 *   6) `sizeof(L"abc")` 按 wchar_t 宽度算（16 不是 4）；
 *   7) `'\377'` 是 -1（按 char 求值再转 int，6.4.4.4p10）；
 *   8) union 的指定初始化器按**那个成员**的类型落值（`.f = 1.0f` 存位模式）；
 *   9) 聚合按值返回：单成员/大结构体在 C99 后端不能返回悬垂指针；
 *  10) 多维 VLA（块作用域）：读写与 sizeof。 */
/* A 清单里所有"真缺陷"的回归集合：每条都和 gcc 比。 */
#include <stdio.h>
#include <string.h>

struct A1 { int a; };
struct N { char c[9]; };
union U { int i; float f; double d; };
struct S { int x, y; };

int A[5] = {10, 20, 30, 40, 50};
int *p2 = A + 2;
int *p0 = &A[0];
struct S s0 = {1, 2};
int *px = &s0.x;

static struct A1 mk1(void) { struct A1 r = {7}; return r; }
static struct N mkn(int x) { struct N n; int i; for (i = 0; i < 9; i++) n.c[i] = (char)(x + i); return n; }

int main(void)
{
	int i = -1;
	short sh = -2;
	signed char sc = -3;
	long l = -2;
	unsigned u = 2;
	int arr[3] = {1, 2, 3};
	int k = 1;
	union U uu = { .f = 1.0f };
	struct A1 a1 = mk1();
	struct N nn = mkn(1);
	int bad = 0;

	/* 1) 有符号窄载入（C99/LLVM 后端曾经零扩展） */
	if ((long long)i != -1 || (long long)sh != -2 || (long long)sc != -3) bad++;
	if (i / 2 != 0 || i >> 1 != -1) bad++;
	/* 2) long / unsigned int 是有符号除法 */
	if (l / u != -1 || l % u != 0) bad++;
	/* 3) 全局地址常量 */
	if (*p2 != 30 || *p0 != 10 || *px != 1) bad++;
	/* 4) 复合字面量后缀、5) 交换律下标 */
	if ((struct S){5, 6}.y != 6 || (int[]){7, 8, 9}[1] != 8) bad++;
	if (k[arr] != 2 || 2[arr] != 3) bad++;
	/* 6) 宽字符串 sizeof、7) 字符常量符号 */
	if ((int)sizeof(L"abc") != 16 || (int)'\377' != -1) bad++;
	/* 8) union 指定初始化器、9) 单成员/大结构体返回 */
	if (uu.i != 1065353216) bad++;
	if (a1.a != 7 || nn.c[0] != 1 || nn.c[8] != 9) bad++;
	/* 10) 多维 VLA（块作用域）与 VLA 形参 */
	{
		int n = 2, m = 3, r, c, t = 0;
		int b[n][m];

		for (r = 0; r < n; r++)
			for (c = 0; c < m; c++)
				b[r][c] = r * 10 + c;
		for (r = 0; r < n; r++)
			for (c = 0; c < m; c++)
				t += b[r][c];
		if (t != 36 || (int)sizeof b != 24) bad++;
	}
	printf("all ok=%d bad=%d\n", bad == 0, bad);
	return bad != 0;
}
