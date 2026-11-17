/* expect: 42 */
/* 静态初始化里被分组括号包起来的字符串字面量：`("n")`、`(("n"))`。
 * 以前 `(` 直接落到常量表达式求值里，报"这里要一个常量表达式"——内核
 * arch/x86/kernel/smp.c 的
 *     register_nmi_handler(NMI_LOCAL, smp_stop_nmi_callback,
 *                          NMI_FLAG_FIRST, "smp_stop")
 * 把字符串当宏实参传，展开出来就是 `.name = ("smp_stop")`。 */
struct nmiaction {
	int (*handler)(unsigned int);
	const char *name;
};

#define REG(fn, n) { .handler = (fn), .name = (n), }

static int handler(unsigned int cmd)
{
	return (int)cmd;
}

static struct nmiaction act = REG(handler, "smp_stop");
static const char *direct = ("plain");
static const char *nested = (("nested"));

int main(void)
{
	if (act.handler(7) != 7)
		return 1;
	if (act.name[0] != 's' || act.name[4] != 's')
		return 2;
	if (direct[0] != 'p')
		return 3;
	if (nested[2] != 's')
		return 4;
	return 42;
}
