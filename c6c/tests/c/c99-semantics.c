/* C99 语言语义回归：只按**退出码**判对错（不打 printf），所以三个后端
 * 都能跑——LLVM 后端目前还没把变参的 double 放进 XMM，凡是要 printf 的
 * 用例在它上面都过不了（既有缺陷，记在 docs/todo.md）。
 *
 * 覆盖这一轮修掉的三处 C99 语义：
 *   1) `sizeof` 变长数组**不带括号**（`sizeof v`，C99 6.5.3.4p2）
 *   2) 转 `_Bool` 一律是"和 0 比"（C99 6.3.1.2）
 *   3) 位域成员的初始化器（含指定初始化器）按位写，不是按字节写整个字
 * 另外顺带守住一批算术/转换/聚合的既有行为（负除取模向零截断、混合符号
 * 比较、long long 移位、聚合赋值与按值传参、复合字面量、`for` 里声明）。 */
struct BF { signed a : 5; unsigned b : 3; int c; };
struct S { int a; char b[5]; double d; };
struct BE { unsigned a : 3, b : 5; };

/* 出参而不是"返回聚合"：聚合按值返回现在是被调方帧里的地址，调用方拿到
 * 就是死指针（既有缺陷，见 docs/todo.md），别让这个用例靠它才过。 */
static void mk(struct S *out, int a)
{
	out->a = a;
	out->b[0] = 'x';
	out->b[1] = 0;
	out->d = a * 0.5;
}

static int takes(struct S s)
{
	return s.a + (int)s.d + s.b[0];
}

static int vla_sz(int n)
{
	int v[n + 1];

	return (int)sizeof v;           /* 不带括号：C99 6.5.3.4p2 */
}

int main(void)
{
	int n = 0;

#define CHK(x) do { if (!(x)) return (1000 + ++n); } while (0)

	/* 1) sizeof 变长数组：带括号不带括号都要是运行期算出来的 */
	{
		int m = 4;
		int v[m];

		CHK(sizeof v == 16);
		CHK(sizeof(v) == 16);
		CHK(sizeof v == sizeof(v));
		CHK((int)sizeof v == 16);
		CHK(vla_sz(3) == 16);
	}
	/* 2) 转 _Bool：非 0 一律 1，浮点同理 */
	{
		_Bool b = 5;
		_Bool c = 0;
		_Bool d = 0.5;
		_Bool e = -1;
		_Bool f = (void *)&n != 0;
		long long big = 1234567890123LL;
		_Bool g = big;
		int i = 300;

		CHK(b == 1);
		CHK(c == 0);
		CHK(d == 1);
		CHK(e == 1);
		CHK(f == 1);
		CHK(g == 1);
		CHK((_Bool)i == 1);
		CHK(b + b == 2);
		CHK((int)sizeof(_Bool) == 1);
		CHK((_Bool)0.0 == 0);
		CHK((_Bool)-0.0 == 0);
		CHK((_Bool)0.0001 == 1);
	}
	/* 3) 位域初始化器 */
	{
		struct BE x = {5, 17};
		struct BE y;
		struct BE z = {.b = 3};

		y.a = 5;
		y.b = 17;
		CHK(x.a == 5 && x.b == 17);
		CHK(x.a == y.a && x.b == y.b);
		CHK(z.a == 0 && z.b == 3);      /* 同字节里的第二个位域 */
		CHK((int)sizeof x == 4);
	}
	/* 4) 位域回绕与符号 */
	{
		struct BF bf;

		bf.a = 15;
		bf.b = 7;
		bf.c = -1;
		CHK(bf.a == 15 && bf.b == 7 && bf.c == -1);
		bf.a++;
		CHK(bf.a == -16);               /* 5 位有符号回绕 */
		bf.b += 3;
		CHK(bf.b == 2);                 /* 3 位无符号回绕 */
	}
	/* 5) 整数转换/算术（C99 的确定行为） */
	CHK((unsigned)-1 == 4294967295u);
	CHK((long long)-1 == -1LL);
	CHK((int)(unsigned char)300 == 44);
	CHK((unsigned char)-1 == 255);
	CHK((short)70000 == 4464);
	CHK((-1 < 0u) == 0);
	CHK((-1 < 1u) == 0);
	CHK(7 / -2 == -3 && 7 % -2 == 1);
	CHK(-7 / 2 == -3 && -7 % 2 == -1);
	CHK((int)-1.9 == -1 && (int)1.9 == 1);
	CHK(1234567890123LL / 1000 == 1234567890LL);
	CHK((1LL << 40) >> 8 == (1LL << 32));
	CHK((1u << 31) == 2147483648u);
	/* 6) 聚合：赋值、按值传参、复合字面量、大括号省略 */
	{
		struct S s1;
		struct S s2;
		struct S tmp;
		int got;
		int *p = (int[3]){4, 5, 6};
		struct S s3 = {7};
		int a[5] = {1, 2};

		mk(&s1, 4);
		s2 = s1;
		s2.a = 5;
		CHK(s1.a == 4 && s2.a == 5);
		mk(&tmp, 2);
		got = takes(tmp);
		CHK(got == 2 + 1 + 'x');
		CHK(p[2] == 6);
		CHK(s3.a == 7 && s3.d == 0.0);
		CHK(a[1] == 2 && a[4] == 0);
	}
	/* 7) 指针算术 */
	{
		int a[4] = {10, 20, 30, 40};
		int *q = a + 4;

		CHK(q - a == 4);
		CHK(*(a + 2) == 30);
		CHK((char *)a + 8 == (char *)(a + 2));
		CHK(&a[3] == q - 1);
	}
	/* 8) C99 的 for 里声明 */
	{
		int s = 0;

		for (int i = 0; i < 4; i++)
			s += i;
		CHK(s == 6);
	}
#undef CHK
	return 0;
}
