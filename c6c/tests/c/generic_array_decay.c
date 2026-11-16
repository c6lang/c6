/* expect: 42 */
/* `_Generic` 的控制表达式要先做**左值转换**：数组退化成指向首元素的指针。
 *
 * C11 6.5.1.1 规定控制表达式经过左值转换（lvalue conversion）之后才取类型，
 * 数组因此退化成指针。内核 include/linux/sysfs.h:297 的
 *
 *     #define ATTRIBUTE_GROUPS(_name)                          \
 *     static const struct attribute_group _name##_group = {     \
 *             .attrs = _Generic(_name##_attrs,                  \
 *                               struct attribute **:            \
 *                                     _name##_attrs,            \
 *                               const struct attribute *const *: \
 *                                     (void *)_name##_attrs     \
 *             ),                                                \
 *     };                                                        \
 *     __ATTRIBUTE_GROUPS(_name)
 *
 * 就靠这一条：`_name##_attrs` 是 `struct attribute *[]`，gcc 拿它退化成
 * `struct attribute **` 去比。c6c 以前拿裸数组类型比，两个分支都对不上，
 * 报"_Generic 没有匹配的类型分支"（实测跨目录 9 个文件）。
 *
 * 顺带把限定符也去掉：`const int` 和 `int` 必须是同一个关联分支。
 */
struct attribute;

static struct attribute *foo_attrs[] = { 0 };
static const int cval = 7;

/* 数组 → 指针 */
static struct attribute **foo_as_ptr(void)
{
	return _Generic(foo_attrs,
			struct attribute **: foo_attrs,
			const struct attribute *const *: (void *)foo_attrs);
}

/* 限定符去掉 */
static int cval_probe(void)
{
	return _Generic(cval, int: 1, default: 0);
}

/* 文件作用域也用一遍：这正是 ATTRIBUTE_GROUPS 的形状 */
static void *foo_glob = _Generic(foo_attrs,
				 struct attribute **: foo_attrs,
				 default: (void *)0);

/* 函数指示符也要退化成函数指针 */
static int target_fn(void)
{
	return 1;
}

static int (*fn_ptr)(void) = _Generic(target_fn, int (*)(void): target_fn);

int main(void)
{
	if (foo_as_ptr() != foo_attrs)
		return 1;
	if (cval_probe() != 1)
		return 2;
	if (foo_glob != (void *)foo_attrs)
		return 3;
	if (fn_ptr() != 1)
		return 4;
	/* 非数组、无符号的常见分支照旧 */
	if (_Generic(1, int: 5, default: 0) != 5)
		return 5;
	if (_Generic((char)1, char: 6, default: 0) != 6)
		return 6;
	return 42;
}
