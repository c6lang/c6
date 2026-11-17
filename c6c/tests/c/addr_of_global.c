/* expect: 42 */
/* `&符号` 出现在全局初始化器里（成员初始化、加括号、装进整型字段）。
 * 常量层以前只认 `&((T *)0)->member` 这种 offsetof 写法，别的一律报
 * "常量表达式里只支持 &((T *)0)->member 形式的取地址"。内核里到处是这些变体：
 *   .driver_data = (kernel_ulong_t)(&i830_early_ops)   arch/x86/kernel/early-quirks.c
 *   DECLARE_WORK(n, &fn)          → .func = (&fn)      kernel/backtracetest.c
 *   cpumask_bits(&mask)           → &mask.bits[0]      kernel/watchdog.c
 * 它们落地都该是"符号 + 偏移"的重定位，不是符号下标那个整数。 */
struct ops {
	int id;
	int pad;
};

struct dev {
	const struct ops *ops;
	unsigned long ops_addr;
};

struct wq {
	void (*fn)(void);
};

static struct ops my_ops = { 1, 2 };
static struct ops other_ops = { 3, 4 };

static void plain_fn(void) {}
static void other_fn(void) {}

/* 成员初始化里带括号的 &全局 */
static struct dev d1 = { .ops = (&my_ops) };
/* 不带括号的 */
static struct dev d2 = { .ops = &my_ops };
/* 把地址装进整型字段（真实代码里是 kernel_ulong_t） */
static struct dev d3 = { .ops_addr = (unsigned long)(&other_ops) };
/* 整型全局直接接地址 */
static unsigned long addr = (unsigned long)(&my_ops);
/* 函数指针带括号 */
static struct wq w1 = { .fn = (&plain_fn) };
/* `&sym.member`：取全局对象里某个成员的地址 */
static const struct ops *mp = &my_ops.pad;

int main(void)
{
	int r = 0;

	if (d1.ops == &my_ops)
		r += 1;
	if (d2.ops == &my_ops)
		r += 2;
	if (d3.ops_addr == (unsigned long)&other_ops)
		r += 4;
	if (addr == (unsigned long)&my_ops)
		r += 8;
	if (w1.fn == &plain_fn)
		r += 16;
	if (d1.ops != &my_ops && d1.ops != &other_ops)
		return 1;
	if (w1.fn == &other_fn)
		return 1;
	if (mp != (const struct ops *)&my_ops.pad)
		return 1;
	return r + 11;
}
