/* expect: 7 */
/* 零字节聚合当初始化目标：有些结构体在某些配置下就是空的（内核
 * include/linux/userfaultfd_k.h 的 `struct vm_userfaultfd_ctx {}`），
 * `= {}` / `= 另一个 empty 对象` 都没有字节要写，以前报
 * "空的聚合类型不能这样初始化"。全局和块作用域两条路都要放行。 */
typedef struct {} empty_s;

struct Holder {
	int v;
	empty_s e;
};

empty_s ge = {};
struct Holder gh = { .v = 7, .e = {} };

int main(void)
{
	empty_s le = {};
	empty_s le2 = le;
	struct Holder lh = { .v = 3, .e = le2 };

	if (gh.v != 7 || lh.v != 3)
		return 1;
	(void)ge;
	(void)le;
	return gh.v + lh.v - 3;
}
