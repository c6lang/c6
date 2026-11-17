/* expect: 42 */
/* 区间设计符 `[lo ... hi]`：lo/hi 可以是**表达式**，不只一个数字字面量。
 * 以前 init_body 只看第 3 个 token 是不是 `...`，表达式下界直接漏判，报
 * "这里要 ']'（当前是 '...'）"。内核 arch/x86/kernel/dumpstack_64.c 的
 * EPAGERANGE(st) 就是这个形状（`[PFN_DOWN(...) ... PFN_DOWN(...) - 1]`）。 */
struct ep {
	unsigned long offs;
	unsigned short size;
	unsigned short type;
};

#define OFF(st)   ((st) * 4)
#define SZ(st)    (2 + (st))
#define RANGE(st)							\
	[OFF(st) ...							\
	 OFF(st) + SZ(st) - 1] = {					\
		.offs	= OFF(st),					\
		.size	= SZ(st),					\
		.type	= 3 + (st), }

static const struct ep tab[32] = {
	RANGE(0),
	RANGE(1),
	RANGE(2),
};

int main(void)
{
	/* RANGE(0) → [0 ... 1]，RANGE(1) → [4 ... 6]，RANGE(2) → [8 ... 11] */
	if (tab[0].offs != 0 || tab[0].size != 2 || tab[0].type != 3)
		return 1;
	if (tab[1].offs != 0 || tab[1].size != 2)
		return 2;
	if (tab[2].offs != 0 || tab[2].size != 0)
		return 3;          /* 区间之间是数据段本来的 0 */
	if (tab[4].offs != 4 || tab[4].size != 3 || tab[4].type != 4)
		return 4;
	if (tab[6].offs != 4 || tab[6].size != 3)
		return 5;
	if (tab[7].offs != 0)
		return 6;
	if (tab[8].offs != 8 || tab[8].size != 4 || tab[8].type != 5)
		return 7;
	if (tab[11].size != 4)
		return 8;
	if (tab[12].offs != 0)
		return 9;
	return 42;
}
