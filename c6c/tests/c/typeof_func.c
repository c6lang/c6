/* expect: 42 */
/* `typeof(函数指示符)` 必须给出**函数类型本身**，不能退化成函数指针。
 *
 * 这条决定 `extern typeof(f) v;` 声明出来的是"函数 v"还是"函数指针变量 v"。
 * 内核 arch/x86/include/asm/bug.h 的
 *     extern typeof(__WARN_trap) __SCT__WARN_trap;
 * 后面紧跟 `(&__SCT__WARN_trap)(args)`：只有当 v 是**函数**时，`&v` 才是
 * `f 的类型 *`，整个调用才编得对。退化成指针的话 `&v` 是"指向指针的指针"。
 *
 * 三元/二元运算里的操作数照常退化（C 标准）：`typeof(1?f:f)` 是函数指针类型。
 * 这一条也在这里钉住，免得把 NO_DECAY（typeof 的"别退化"标记）漏到子表达式。 */
#include <stdio.h>

static int f(int a) { return a + 1; }

/* 函数类型声明：不需要 `=` 初始化器，不占数据段。内核那条也是 extern，
 * 定义在别处；这里只验证"能声明、类型对"，不做链接。 */
extern typeof(f) v;

/* typeof 拿到的必须是函数类型：`typeof(f) *` 是"指向函数的指针" */
static typeof(f) *fp = f;

int main(void)
{
	/* 三元里退化：`1 ? f : f` 是函数指针，可以赋给函数指针变量 */
	int (*g)(int) = (1 ? f : f);
	/* 指针解引用回函数调用 */
	int r = (*g)(41);

	/* v 的类型必须是**函数**：`typeof(v) *` 才是"指向函数的指针"。
	 * 退化成指针的话这里会是"指向指针的指针"，赋值就不成立。 */
	{
		typeof(v) *pv = f;

		if ((*pv)(41) != 42)
			return 3;
	}
	if (r != 42)
		return 1;
	if ((*fp)(41) != 42)
		return 2;
	return 42;
}
