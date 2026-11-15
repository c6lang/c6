/* 有符号窄整型载入（C99 6.3.1.1 / 6.5.5 / 6.5.7）：三个后端必须逐字节一致。
 *
 * 这条用例是给 C99 输出后端和 LLVM 后端立的门。它们以前从内存读 1/2/4 字节
 * 时**一律零扩展**（C99 的 c6_ld1/2/4、LLVM 的 zext），于是 `int i = -1`
 * 读回来是 4294967295：符号提升、除法、算术右移、int→double 全跟着错。
 * native 后端用 movsx/movsxd，是对的，所以这个缺陷只在另两个后端露头
 * （也是"三后端互为尺子"的用处：native 对、另两个错，说明错在翻译层）。
 *
 * 覆盖：局部变量的窄类型、内存里读回来（全局标量 / 数组 / 结构体成员 /
 * 指针解引用）、窄类型形参与返回值、signed/unsigned 关系比较，以及
 * IRT_U8/U16/U32 的**零扩展对照**——修有符号载入时不能把无符号一起 sext 了。
 *
 * 输出要和 native 后端逐字节一致（tests/c99-diff.sh、llvm-diff.sh 比的是
 * 标准输出 + 退出码），所以这里尽管打；return 值是自检结果，坏了一律非 0。 */
#include <stdio.h>

/* 全局带初值：走数据段 + IR_LOAD，和局部变量是两条不同的路径 */
signed char g_c = -3;
short g_s = -2;
int g_i = -1;
unsigned char g_uc = 0xFF;
unsigned short g_us = 0xFFFF;
unsigned int g_ui = 0xFFFFFFFFu;

struct S {
	signed char c;
	short s;
	int i;
	unsigned char uc;
	unsigned short us;
	unsigned int ui;
};

static struct S g_st = { -4, -5, -6, 200, 40000, 4000000000u };

/* 窄类型返回值：被调方把 -7 放进返回值，调用方还得按有符号解释一遍 */
static signed char ret_c(void) { return -7; }
static short ret_s(void) { return -300; }
static int ret_i(void) { return -70000; }

/* 窄类型形参：实参先截断进形参槽，函数体里再 load 回来 */
static int take_c(signed char c) { return c; }
static int take_s(short s) { return s; }
static int take_i(int i) { return i; }

static int bad;

static void chk(int cond, int code)
{
	if (!cond && !bad)
		bad = code;
}

int main(void)
{
	int i = -1;
	short s = -2;
	signed char c = -3;
	long long li = i;           /* 符号提升：-1 而不是 4294967295 */
	int h = i / 2;              /* 向零截断 => 0（不是算术右移的 -1） */
	int m = i % 2;              /* => -1 */
	int sh = i >> 1;            /* 算术右移 => -1 */
	double d = i;               /* int→double：-1.0 */
	double dd = i / 2.0;
	long long ls = s;
	int cs = c;

	printf("locals %lld %d %d %d %g %g %lld %d\n", li, h, m, sh, d, dd, ls, cs);
	chk(li == -1, 1);
	chk(h == 0, 2);
	chk(m == -1, 3);
	chk(sh == -1, 4);
	chk(d == -1.0, 5);
	chk(ls == -2, 6);
	chk(cs == -3, 7);

	/* 负数除法/取模向零截断，右移是算术右移 */
	{
		int a = -7, b = 2;
		long long la = -9, lb = 4;
		int arr[4];
		int k;

		printf("divmod %d %d %lld %lld\n", a / b, a % b, la / lb, la % lb);
		chk(a / b == -3, 8);
		chk(a % b == -1, 9);
		chk(la / lb == -2, 10);
		chk(la % lb == -1, 11);
		printf("sar %d %d %lld\n", a >> 1, -1 >> 3, la >> 2);
		chk((a >> 1) == -4, 12);
		chk((-1 >> 3) == -1, 13);
		chk((la >> 2) == -3, 14);
		/* 数组元素：写进去是负的，按 int 读回来也得是负的 */
		arr[0] = -1;
		arr[1] = -2;
		arr[2] = -3;
		arr[3] = -4;
		for (k = 0; k < 4; k++)
			printf("arr[%d]=%d,%d ", k, arr[k], arr[k] / 2);
		printf("\n");
		chk(arr[0] == -1 && arr[3] == -4, 15);
		chk(arr[1] / 2 == -1, 16);
	}

	/* 内存里读回来：全局标量 / 结构体成员 / 指针解引用 */
	{
		signed char *pc = &g_c;
		short *ps = &g_s;
		int *pi = &g_i;

		printf("globals %d %d %d %u %u %u\n", g_c, g_s, g_i, g_uc, g_us, g_ui);
		chk(g_c == -3 && g_s == -2 && g_i == -1, 17);
		chk(g_uc == 255 && g_us == 65535 && g_ui == 4294967295u, 18);
		printf("ptr %d %d %d\n", *pc, *ps, *pi);
		chk(*pc == -3 && *ps == -2 && *pi == -1, 19);
		printf("struct %d %d %d %u %u %u\n", g_st.c, g_st.s, g_st.i,
		       g_st.uc, g_st.us, g_st.ui);
		chk(g_st.c == -4 && g_st.s == -5 && g_st.i == -6, 20);
		chk(g_st.uc == 200 && g_st.us == 40000 && g_st.ui == 4000000000u, 21);
		/* 有符号成员升到 long long / double：符号位要跟着上去 */
		printf("s2ll %lld %lld %lld %lld\n", (long long)g_st.c, (long long)g_st.s,
		       (long long)g_st.i, (long long)g_st.uc);
		chk((long long)g_st.c == -4 && (long long)g_st.i == -6, 22);
		chk((long long)g_st.uc == 200, 23);
		printf("s2d %g %g %g %g\n", (double)g_st.c, (double)g_st.s,
		       (double)g_st.i, (double)g_st.uc);
		chk((double)g_st.i == -6.0 && (double)g_st.s == -5.0, 24);
	}

	/* 函数返回有符号窄类型 / 收有符号窄类型形参 */
	printf("ret %d %d %d\n", ret_c(), ret_s(), ret_i());
	chk(ret_c() == -7 && ret_s() == -300 && ret_i() == -70000, 25);
	printf("arg %d %d %d\n", take_c(-11), take_s(-12000), take_i(-1));
	chk(take_c(-11) == -11 && take_s(-12000) == -12000, 26);

	/* signed / unsigned 的关系比较：有符号那边要按有符号比 */
	{
		unsigned char uc = 0xFF;
		unsigned short us = 0xFFFF;
		unsigned int ui = 0xFFFFFFFFu;
		int neg = -1;

		printf("cmp %d %d %d %d %d %d\n", neg < 0, uc > 0, us > 0, ui > 0,
		       neg < c, uc > 200);
		chk(neg < 0, 27);
		chk(uc > 0 && us > 0 && ui > 0, 28);
		chk(!(neg < c), 29);        /* -1 < -3 是假 */
		chk(uc > 200, 30);
	}

	/* 无符号对照：必须继续零扩展（0x80 不能变 -128） */
	{
		unsigned char uc = 0x80;
		unsigned short us = 0xFFFE;
		unsigned int ui = 0x80000000u;
		unsigned long long ull;

		ull = uc;
		printf("unsigned %llu ", ull);
		chk(ull == 128, 31);
		ull = us;
		printf("%llu ", ull);
		chk(ull == 65534, 32);
		ull = ui;
		printf("%llu\n", ull);
		chk(ull == 2147483648u, 33);
	}

	return bad;
}
