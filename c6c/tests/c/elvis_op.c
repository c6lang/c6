/* expect: 42 */
/* GNU 省略中间操作数的三元 `a ?: b`（elvis），以及结构体级
 * `} __attribute__((aligned(表达式)))` 两件事一起钉。
 *
 * 单独看是两条无关的语法，实际是同一处内核断点：include/linux/cache.h 的
 *     __cacheline_group_begin(g)\
 *         __u8 __cacheline_group_begin__##g[0] \
 *             __aligned__((+0) ? : (1 << L1_CACHE_SHIFT));
 * 展开成 `__aligned__( ( + 0) ? : ( 1 << 6) )`。
 *
 * 以前 aligned 的值只认一个数字字面量，所以这条根本没进常量求值；等改成
 * 按常量表达式求之后，常量层缺 `?:` 的窟窿才露出来（lib/ 下 6 个文件
 * 同时报"这里要 ':'（当前是 '('）"）。
 *
 * 结构体那条对应内核 include/linux/mm_types.h 的 struct page：
 *     } __attribute__((__aligned__(2 * sizeof(unsigned long))));
 * 对齐 16 要把 56 字节的体向上取整成 64，少了它就是 56，于是
 * mm/slab 的 static_assert(sizeof(struct slab) <= sizeof(struct page)) 假失败。
 */
struct aligned_expr {
	long a;
} __attribute__((__aligned__(2 * sizeof(unsigned long))));

int main(void)
{
	int zero = 0;
	int n = 7;
	int calls = 0;

	/* 结构体级 aligned 的值要按常量表达式求，不是只认第一个数字字面量 */
	_Static_assert(sizeof(struct aligned_expr) == 16, "aligned 尺寸向上取整");
	_Static_assert(_Alignof(struct aligned_expr) == 16, "aligned 对齐值");

	/* 常量层里的 `?:` */
	_Static_assert((5 ?: 9) == 5, "非 0 取自身");
	_Static_assert((0 ?: 9) == 9, "0 取右边");
	_Static_assert(((1 << 6) ?: (1 << 3)) == 64, "aligned 那种形状");

	/* 运行期：条件**只求值一次**，非 0 时结果就是条件那个值 */
	if ((n++ ?: 100) != 7)
		return 1;
	if (n != 8)
		return 2;
	if ((zero ?: 100) != 100)
		return 3;
	if (zero != 0)
		return 4;
	/* 右边只在条件为 0 时才求值 */
	if ((n ?: (calls++) ) != 8)
		return 5;
	if (calls != 0)
		return 6;
	if ((zero ?: (calls++)) != 0)
		return 7;
	if (calls != 1)
		return 8;
	return 42;
}
