/* expect: 42 */
/* `typedef` 的基类型本身就是指针时，`(*name)` 里那层指针要归**声明符**，
 * 基类型自带的那层要留在函数后缀里面：
 *     typedef void *own_t;
 *     own_t (*get)(own_t);      // 指向「返回 void* 的函数」的指针
 * 以前 decl_fill_hole 一路剥到"第一层非指针"才停，把基类型那层也当成外层
 * 指针扔到函数外面，于是 get 变成「指向『返回 void 的函数』的指针的指针」，
 * `get(p)` 报"这里要 ';'（当前是 '('）"。内核 fs/locks.c 的
 * `fl_owner_t (*lm_get_owner)(fl_owner_t)` 正是这个形状。 */
typedef void *own_t;
typedef int *ip;

own_t (*get)(own_t);
ip (*getip)(ip);

static void *impl(void *p)
{
	return p;
}

int main(void)
{
	struct { char tag[8]; } obj;
	ip n = 0;

	get = impl;
	if (get(&obj) != &obj)
		return 1;
	if (get(0) != 0)
		return 2;
	if (getip != 0)
		return 3;
	if (n != 0)
		return 4;
	return 42;
}
