/* C99 三字符组（6.4.6，翻译阶段 1）：`??X` 要在词法分析之前换成单个字符。
 * 这个文件本身是带三字符组写的——注意它**过不了** gcc 的默认设置
 * （gcc 只在 -std=c99/-trigraphs 下认），所以这里只给 c6c 的三后端比。
 *
 * 判对错只看退出码（LLVM 后端的变参还没把 double 放进 XMM，别用 printf）。 */
??=define ARR_N 3
??=define SPLICE 1 ??/
	+ 1

struct S ??< int a; int b; ??>;          /* `{ }` */

int main(void)
{
	int a??(ARR_N??) = ??<1, 2, 3??>;    /* `[ ]`、`{ }` */
	struct S s = ??<4, 5??>;
	const char *p = "a??!b";             /* 字符串里也要换：`|` */
	const char *q = "c??-d";             /* `~` */
	const char *r = "e??'f";             /* `^` */
	int v = 5 ??! 2;                     /* 5 | 2 = 7 */
	int w = ??-0;                        /* ~0 = -1 */
	int x = 0;

	v = v ??' 1;                         /* 7 ^ 1 = 6 */
	if (v != 6 || w != -1)
		return 1;
	if (SPLICE != 2)
		return 2;
	if (a[0] != 1 || a[2] != 3 || s.a != 4 || s.b != 5)
		return 3;
	if (p[1] != '|' || q[1] != '~' || r[1] != '^')
		return 4;
	if (!(1 ??!??! 0))                   /* 1 || 0 */
		return 5;
	??< int z = 7; x = z; ??>            /* 语句块用三字符组写 */
	if (x != 7)
		return 6;
	return 0;
}
