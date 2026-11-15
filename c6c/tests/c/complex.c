/* C99 复数（_Complex）：三后端一致性用例。
 *
 * 只用退出码判对错（不打 printf）：LLVM 后端的变参还没把 double 放进 XMM，
 * 那条是既有缺陷，和复数无关，别让这个用例挂在它上面。
 *
 * 覆盖：sizeof/对齐、声明与初始化（局部/全局/聚合成员/数组）、+ - * / 与
 * 一元负号、复合赋值、== / !=、实数↔复数转换（隐式与显式 cast）、
 * 按值传参/返回、GNU 的 __real__ / __imag__（读写分量）。 */
#define CHECK(n, cond) do { if (!(cond)) return (n); } while (0)

typedef double _Complex DComplex;

static double _Complex g_one = 1.0;        /* 全局用实数初始化 */
static DComplex g_arr[2];
struct Holder { int tag; double _Complex v; };

static double _Complex cmul(double _Complex a, double _Complex b)
{
	return a * b;
}

static double re_of(double _Complex z)
{
	return __real__ z;
}

static double im_of(double _Complex z)
{
	return __imag__ z;
}

static double _Complex make(double re, double im)
{
	double _Complex z = re;

	__imag__ z = im;
	return z;
}

int main(void)
{
	double _Complex a;
	double _Complex b;
	double _Complex c;
	struct Holder h;
	float _Complex fz;
	double d;

	/* 1) sizeof / 分量宽度：复数就是两个分量，所以是 2 倍（c6c 的
	 * long double 和 double 一样是 8 字节，这么写两边都成立） */
	CHECK(1, sizeof(double _Complex) == 2 * sizeof(double));
	CHECK(2, sizeof(float _Complex) == 2 * sizeof(float));
	CHECK(3, sizeof(long double _Complex) == 2 * sizeof(long double));

	/* 2) 构造与分量读写 */
	a = make(3.0, 4.0);
	b = make(1.0, -2.0);
	CHECK(4, __real__ a == 3.0 && __imag__ a == 4.0);
	CHECK(5, re_of(a) == 3.0 && im_of(a) == -2.0 * -2.0);

	/* 3) 四则： (3+4i)(1-2i) = 11-2i */
	c = cmul(a, b);
	CHECK(6, __real__ c == 11.0 && __imag__ c == -2.0);
	/* (3+4i)/(1-2i) = (-1+2i) */
	c = a / b;
	CHECK(7, __real__ c == -1.0 && __imag__ c == 2.0);
	c = a + b;
	CHECK(8, __real__ c == 4.0 && __imag__ c == 2.0);
	c = a - b;
	CHECK(9, __real__ c == 2.0 && __imag__ c == 6.0);
	c = -a;
	CHECK(10, __real__ c == -3.0 && __imag__ c == -4.0);

	/* 4) 复合赋值 */
	c = a;
	c *= b;
	CHECK(11, __real__ c == 11.0 && __imag__ c == -2.0);
	c /= b;
	CHECK(12, __real__ c == 3.0 && __imag__ c == 4.0);
	c += 1.0;                       /* 实数参与常用算术转换 */
	CHECK(13, __real__ c == 4.0 && __imag__ c == 4.0);
	c -= 1.0;
	CHECK(14, __real__ c == 3.0 && __imag__ c == 4.0);

	/* 5) 比较：只有 == / != */
	CHECK(15, a == a);
	CHECK(16, !(a != a));
	CHECK(17, a != b);
	CHECK(18, b == make(1.0, -2.0));

	/* 6) 转换 */
	c = 3;                          /* 整数 → 复数 */
	CHECK(19, __real__ c == 3.0 && __imag__ c == 0.0);
	c = (double _Complex)2.5;
	CHECK(20, __real__ c == 2.5 && __imag__ c == 0.0);
	d = (double)a;                  /* 复数 → 实数取实部 */
	CHECK(21, d == 3.0);
	fz = (float _Complex)a;
	CHECK(22, (double)__real__ fz == 3.0 && (double)__imag__ fz == 4.0);
	fz = (float _Complex)(0.5 + 0.25);
	CHECK(23, (double)__real__ fz == 0.75);

	/* 7) 聚合成员 / 数组 / 全局 */
	h.tag = 7;
	h.v = a;
	CHECK(24, h.tag == 7 && __real__ h.v == 3.0 && __imag__ h.v == 4.0);
	g_arr[1] = b;
	CHECK(25, __real__ g_arr[1] == 1.0 && __imag__ g_arr[1] == -2.0);
	CHECK(26, __real__ g_one == 1.0 && __imag__ g_one == 0.0);
	g_arr[0] = g_one * g_one;
	CHECK(27, __real__ g_arr[0] == 1.0 && __imag__ g_arr[0] == 0.0);

	/* 8) 一元 + 与三目 */
	c = +a;
	CHECK(28, c == a);
	c = (__real__ a > 0) ? a : b;
	CHECK(29, c == a);

	return 0;
}
