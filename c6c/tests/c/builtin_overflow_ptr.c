/* expect: 42 */
/* `__builtin_{add,sub,mul}_overflow(a, b, out)` 的第三个实参是个**表达式**
 * （指向结果的指针），不是类型名。c6c 以前把它按 parse_typename 读：
 *   - 实参写成变量/形参时整个被当成类型名吃光，紧跟的 `)` 报
 *     "这里要一个表达式（当前是 ')'）"（内核 drivers/base/devres.c:108、
 *     fs/namespace.c 都是 `check_add_overflow(x, y, tot_size)` 这个形状）；
 *   - 写成 `&d` 时虽然能过，拿到的指向类型也不对，`unsigned long d` 会按
 *     int 判溢出——0xFFFFFFFFUL + 1 被判成溢出，而 gcc 判不溢出。
 * 现在类型从表达式自己身上取（GEN_TY），两种写法都对，宽度也对。 */

/* 结果指针是形参：内核里的常见形状 */
static int add_ptr(int a, int b, int *out)
{
	return __builtin_add_overflow(a, b, out);
}

static int add_ulong_ptr(unsigned long a, unsigned long b, unsigned long *out)
{
	return __builtin_add_overflow(a, b, out);
}

static int sub_ptr(int a, int b, int *out)
{
	return __builtin_sub_overflow(a, b, out);
}

static int mul_ptr(int a, int b, int *out)
{
	return __builtin_mul_overflow(a, b, out);
}

int main(void)
{
	int d = 0;
	unsigned long u = 0;
	int r = 0;

	/* 形参写法：不溢出 + 结果落到 d */
	d = 0;
	if (add_ptr(10, 20, &d) == 0 && d == 30)
		r += 1;
	/* 形参写法：真的溢出（INT_MAX + 1）*/
	d = 0;
	if (add_ptr(0x7FFFFFFF, 1, &d) == 1)
		r += 2;
	/* unsigned long 的宽度：0xFFFFFFFF + 1 放得下，不溢出 */
	if (add_ulong_ptr(0xFFFFFFFFUL, 1, &u) == 0 && u == 0x100000000UL)
		r += 4;
	/* unsigned int 真的溢出（判据在目标宽度上做，32 位这一档是准的）。
	 * 注意 64 位目标宽度上的真回绕仍然看不出来：内部按 64 位算完再截回
	 * 64 位与原值比，等于没截。`check_add_overflow(~0UL, 1, &ul)` 因此会
	 * 报"不溢出"（gcc 报溢出）。这是既有实现的边界，不在本用例的断言范围。 */
	{
		unsigned int w = 0;
		unsigned int wa = ~0U;

		if (__builtin_add_overflow(wa, 1u, &w) == 1)
			r += 8;
	}
	/* sub / mul 也走同一条路 */
	d = 0;
	if (sub_ptr(10, 3, &d) == 0 && d == 7)
		r += 16;
	d = 0;
	if (mul_ptr(6, 7, &d) == 0 && d == 42)
		r += 11;
	/* 直接传指针变量（不带 &） */
	d = 0;
	{
		int *p = &d;

		if (__builtin_add_overflow(1, 2, p) == 0 && d == 3)
			r += 0;
		else
			return 1;
	}
	return r;
}
