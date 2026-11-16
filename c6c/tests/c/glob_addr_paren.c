/* expect: 42 */
/* 全局地址初始化器里，名字外面可以套**成组的括号**。
 *
 * 内核里三种写法到处都是：
 *
 *     include/linux/list.h:27
 *         #define LIST_HEAD_INIT(name) { &(name), &(name) }
 *     include/linux/workqueue.h:241
 *         #define __WORK_INITIALIZER(n, f) { .entry = { &(n).entry, &(n).entry }, … }
 *     include/linux/compiler.h:300
 *         #define ASSERT_STATIC_STORAGE(name) \
 *                 static typeof(name) * const name##_storage_check = &(name)
 *
 * 以前 c6c 只认 `&名字`，看见 `&(` 就报"全局地址初始化器只支持 &全局/函数"；
 * 或者把剥掉括号这件事做在调用方、解析器看不到括号，于是 `&(n).entry` 只剩
 * "取 n 的地址"、后面的 `.entry` 没人接，报"这里要 '}'（当前是 '.'）"。
 * 实测跨目录 15 个文件卡在这条。
 */
struct lh {
	struct lh *next;
	struct lh *prev;
};

struct wk {
	struct lh entry;
	int dummy;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define __WORK_INITIALIZER(n) { .entry = { &(n).entry, &(n).entry }, .dummy = 7 }
#define ASSERT_STATIC_STORAGE(name) \
	static typeof(name) * const name##_storage_check = &(name)
/* 括号和成员**交错嵌套**：workqueue.h:249 的 __DELAYED_WORK_INITIALIZER
 * 把 n 换成 `(n).work` 再交给 __WORK_INITIALIZER，展开就是
 * `&((n).work).entry`——关一层 `)`、走一格 `.work`、再关一层 `)`。 */
#define __DELAYED_WORK_INITIALIZER(n) { .work = __WORK_INITIALIZER((n).work) }

struct dw {
	struct wk work;
};

static struct lh myhead = LIST_HEAD_INIT(myhead);
static struct wk mywork = __WORK_INITIALIZER(mywork);
ASSERT_STATIC_STORAGE(mywork);
static struct dw mydwork = __DELAYED_WORK_INITIALIZER(mydwork);

int main(void)
{
	/* LIST_HEAD_INIT：两条指针都指回自己 */
	if (myhead.next != &myhead)
		return 1;
	if (myhead.prev != &myhead)
		return 2;
	/* __WORK_INITIALIZER：`.entry` 的两个指针指回自己的 .entry */
	if (mywork.entry.next != &mywork.entry)
		return 3;
	if (mywork.entry.prev != &mywork.entry)
		return 4;
	if (mywork.dummy != 7)
		return 5;
	/* ASSERT_STATIC_STORAGE：`&(name)` 就是它自己的地址 */
	if (mywork_storage_check != &mywork)
		return 6;
	/* 交错嵌套：`&((n).work).entry` */
	if (mydwork.work.entry.next != &mydwork.work.entry)
		return 7;
	if (mydwork.work.entry.prev != &mydwork.work.entry)
		return 8;
	if (mydwork.work.dummy != 7)
		return 9;
	return 42;
}
