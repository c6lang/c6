/* expect: 42 */
/* 文件作用域的复合字面量初始化器，元素里**多套一层括号**也要认出来。
 *
 * 内核 include/linux/rbtree_types.h 的
 *     #define RB_ROOT (struct rb_root) { NULL, }
 * 展开成
 *     static struct rb_root bdi_tree = (struct rb_root) { ( (void * )0) , } ;
 *
 * pa_is_compound_lit() 判"这是不是复合字面量"时，depth 从 0 起算——可下标 0 的
 * 那个 `(` 已经站在里面了。于是第一个 `)` 就把 depth 数成 -1，花括号体里的
 * 括号对又把它拉回 0，函数提前 return 一个 false：复合字面量没认出来，值被当
 * 标量送进 const_expr，在 `{` 上报"这里要一个常量表达式"。
 * 实测跨目录 10 个文件卡在这条（mm/backing-dev.c、mm/vmalloc.c、
 * lib/rbtree_test.c、kernel/crash_core_test.c …）。
 */
struct rb_node {
	int x;
};

struct rb_root {
	struct rb_node *rb_node;
};

struct rb_root_cached {
	struct rb_root rb_root;
	struct rb_node *rb_leftmost;
};

#define RB_ROOT (struct rb_root) { ((void *)0), }
#define RB_ROOT_CACHED (struct rb_root_cached) { { ((void *)0), }, ((void *)0) }

static struct rb_root bdi_tree = RB_ROOT;
static struct rb_root_cached cached_root = RB_ROOT_CACHED;

/* 逗号后面留空（`{ NULL, }`）也行 */
static struct rb_root trailing = (struct rb_root) { ((void *)0), };

int main(void)
{
	if (bdi_tree.rb_node != 0)
		return 1;
	if (cached_root.rb_root.rb_node != 0)
		return 2;
	if (cached_root.rb_leftmost != 0)
		return 3;
	if (trailing.rb_node != 0)
		return 4;
	return 42;
}
