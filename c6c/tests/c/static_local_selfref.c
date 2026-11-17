/* expect: 42 */
/* 静态局部在**自己的初始化器里**自引用：`static struct list_head y =
 * LIST_HEAD_INIT(y);`。符号只按改名后的 `函数名.N` 登记，原名要等
 * gen_one_global 回来才挂上，所以初始化器里的 `&y` 以前查不到，
 * 报"'&y' 不是地址常量"。内核的 LIST_HEAD_INIT / __WORK_INITIALIZER
 * 全是这个形状（arch/x86/kernel/apic/hw_nmi.c 的 register_nmi_handler 等）。 */
struct list_head {
	struct list_head *next, *prev;
};
#define LIST_HEAD_INIT(name) { &(name), &(name) }

static struct list_head gx = LIST_HEAD_INIT(gx);

int main(void)
{
	static struct list_head y = LIST_HEAD_INIT(y);

	if (gx.next != &gx || gx.prev != &gx)
		return 1;
	if (y.next != &y || y.prev != &y)
		return 2;
	return 42;
}
