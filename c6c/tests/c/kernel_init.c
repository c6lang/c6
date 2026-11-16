/* expect: 42 */
/* 全局初始化器里几种内核风格的写法。
 *
 *   - 复合字面量当聚合成员的值：`.lock = (raw_t){ .v = 0 }`，
 *     等价于内核 __RAW_SPIN_LOCK_UNLOCKED(lockname) 的展开；
 *   - 分组括号包住的地址常量：`.e1 = ((void *)&vals[0])`，
 *     等价于内核 SYSCTL_ZERO 用到 struct ctl_table 的 .extra1；
 *   - offsetof 带下标：`offsetof(struct T, node[idx])`，
 *     等价于内核 container_of() 的 "node[idx]"；
 *   - 常量层的位运算内建与整型提升。
 *
 * 前两条以前都会静默编错或直接报错：
 *   - 复合字面量改写了层栈，下一个设计符会拿子对象的类型去查成员，
 *     报"结构体没有成员"；
 *   - 分组括号让 cast 剥离循环看不进去，整条落到常量求值里报
 *     "常量表达式里只支持 &((T *)0)->member 形式的取地址"。 */
#include <stdio.h>

typedef struct { int v; } raw_t;
struct S { raw_t lock; unsigned int count; void *fw; };

#define RAW(lockname) (raw_t) { .v = 7 }
#define INIT(name, n) { .lock = RAW(name), .count = n, .fw = 0 }

static struct S g_s = INIT(g_s, 3);

static int vals[5] = { 11, 22, 33, 44, 55 };
struct C { void *e1; void *e2; };
static struct C g_c = { .e1 = ((void *)&vals[0]), .e2 = ((void *)&vals[2]) };

struct N { int v; };
struct T { int a; struct N node[4]; int z; };

/* offsetof 带常量下标 */
enum { OFF2 = __builtin_offsetof(struct T, node[2]) };

int main(void)
{
	if (g_s.lock.v != 7)
		return 1;
	if (g_s.count != 3)
		return 2;
	if (g_s.fw != 0)
		return 3;
	if (*(int *)g_c.e1 != 11)
		return 4;
	if (*(int *)g_c.e2 != 33)
		return 5;
	if (OFF2 != 12)
		return 6;
	return 42;
}
