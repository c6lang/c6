/* expect: 42 */
/* 静态初始化里的几种"取自己/取符号"写法。
 *
 *   - `LIST_HEAD_INIT(name)` 展开成 `{ &(name), &(name) }`：取自身的地址
 *     （内核 include/linux/list.h:27）；
 *   - 成员形式 `&(obj.member)`（LIST_HEAD 放进结构体里时就是这样）；
 *   - 三元挑函数名：`cond ? f1 : f2` 的值是**地址**，要生成重定位，
 *     不能把符号下标当整数写进去（内核 include/linux/debugfs.h:59）。
 */
#include <stdio.h>

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static LIST_HEAD(g_list);

struct holder {
	struct list_head l;
	int v;
};
static struct holder g_h = { .l = LIST_HEAD_INIT(g_h.l), .v = 5 };

static int f1(void) { return 1; }
static int f2(void) { return 2; }

struct fops { int (*pick)(void); };
enum { false_v = 0, true_v = 1 };
static const struct fops g_ops = { .pick = (false_v) ? f1 : f2 };
static const struct fops g_ops2 = { .pick = (true_v) ? f1 : f2 };

int main(void)
{
	if (g_list.next != &g_list || g_list.prev != &g_list)
		return 1;
	if (g_h.l.next != &g_h.l || g_h.v != 5)
		return 2;
	if (g_ops.pick != f2)
		return 3;
	if (g_ops2.pick != f1)
		return 4;
	if (g_ops.pick() != 2 || g_ops2.pick() != 1)
		return 5;
	return 42;
}
