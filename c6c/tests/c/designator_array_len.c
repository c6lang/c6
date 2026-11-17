/* expect: 42 */
/* `T x[] = { [N] = v, … }` 的长度由**填到过的最大下标**推出来。
 * 指针元素走的是"地址常量"那条路（gen_glob_obj），不进 init_store()，于是
 * "填到过哪儿"的标记一直没被推——数组长度算成 0（sizeof 也是 0）。内核
 * lib/stackdepot.c 的
 *     static const char *const counter_names[] = { [DEPOT_…] = "…" };
 *     static_assert(ARRAY_SIZE(counter_names) == DEPOT_COUNTER_COUNT);
 * 就是这么炸的。 */
enum { COL_A, COL_B, COL_C, COL_CNT };

static const char *const names[] = {
	[COL_A] = "a",
	[COL_B] = "b",
	[COL_C] = "c",
};

static void *ptrs[] = {
	[0] = (void *)1,
	[2] = (void *)3,
};

static const int nums[] = { [0] = 1, [1] = 2 };

int main(void)
{
	if (sizeof(names) / sizeof(names[0]) != COL_CNT)
		return 1;
	if (sizeof(ptrs) != 3 * sizeof(void *))
		return 2;
	if (ptrs[1] != 0)
		return 3;
	if (ptrs[2] != (void *)3)
		return 4;
	if (sizeof(nums) != 2 * sizeof(int))
		return 5;
	if (nums[1] != 2)
		return 6;
	return 42;
}
