/* expect: 45 */
/* 静态初始化里 `全局量.数组成员` 是数组退化，属于地址常量（C99 6.6p9）：
 *     .fdt           = &init_files.fdtab,
 *     .close_on_exec = init_files.close_on_exec_init,     （自引用）
 *     .data          = fontdata_8x16.data
 * 以前一律报"取的是运行期的值"，fs/file.c、kernel/utsname_sysctl.c、
 * lib/fonts/font_8x16.c 都栽在这上面。不是数组的成员仍然是运行期的值，
 * gcc 也照样拒绝（`.self = d.p` 就不是常量）。 */
struct src {
	int a[4];
	int tail[2];
	int n;
};

struct dst {
	int *p;
	int *tail;
	int *self;
	int n;
};

struct src g = { { 1, 2, 3, 4 }, { 5, 6 }, 7 };
struct dst d = { .p = g.a, .tail = g.tail, .self = g.tail, .n = 9 };

int main(void)
{
	if (d.p != &g.a[0] || d.p[2] != 3)
		return 1;
	if (d.tail != &g.tail[0] || d.tail[1] != 6)
		return 2;
	if (d.self != &g.tail[0])
		return 3;
	if (d.n != 9)
		return 4;
	return 45;
}
