/* expect: 42 */
/* `typeof(expr)` 后面还能跟限定符和 `*`：
 *     typeof(*(__tmp)->type) const *ptr_const;
 * 内核 include/linux/kfifo.h:64 的 `ptrtype const *ptr_const;` 展开出来正是
 * 这个形状（kfifo 的 union 里那串成员）。以前 parse_typespec 见到 typeof
 * 直接 return，`const` 被当成声明符名字，报"这里要 ';'（当前是 '*'）"。
 * 现在走 typespec_done，后缀限定符那段接着吃掉它。 */
struct holder {
	int *type;
};

static int backing = 5;
static struct holder h = { &backing };

int main(void)
{
	typeof(backing) const *p = &backing;
	typeof(*h.type) const *q = &backing;
	typeof(backing) const scalar = 7;
	typeof(*h.type) const *const frozen = &backing;

	if (p != &backing || *p != 5)
		return 1;
	if (q != &backing || *q != 5)
		return 2;
	if (scalar != 7)
		return 3;
	if (frozen != &backing)
		return 4;
	{
		typeof(backing) * volatile vp = &backing;

		if (*vp != 5)
			return 5;
	}
	return 42;
}
