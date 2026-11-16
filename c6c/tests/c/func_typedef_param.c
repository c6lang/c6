/* expect: 42 */
/* 函数类型的 typedef 当函数形参用（C11 6.9.1p10：自动调整为函数指针）。
 *
 *   typedef int (dist_fn)(unsigned int, unsigned int);
 *   int f(int a, dist_fn d);          <- d 的类型是 int (*)(unsigned, unsigned)
 *
 * `(name)(params)` 这种带括号的声明符、以及形参位置的函数类型退化，两条都要对。
 * 内核 include/linux/percpu.h:110/121 就是 pcpu_fc_cpu_distance_fn_t。 */
#include <stdio.h>

typedef int (dist_fn)(unsigned int from, unsigned int to);
typedef int plain_fn(unsigned int from, unsigned int to);

static int dis(unsigned int a, unsigned int b) { return (int)(b - a); }

static int apply(int a, dist_fn d) { return d((unsigned int)a, 10u); }

static int apply2(int a, plain_fn d) { return d((unsigned int)a, 20u); }

/* 带参数的括号声明式 typedef 也要能取指针 */
static dist_fn *gp = dis;

int main(void)
{
	if (sizeof(dist_fn) != sizeof(plain_fn))
		return 1;
	if (apply(4, dis) != 6)
		return 2;
	if (apply2(5, dis) != 15)
		return 3;
	if ((*gp)(1u, 3u) != 2)
		return 4;
	return 42;
}
