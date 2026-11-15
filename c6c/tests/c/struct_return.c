/* 聚合按值返回（C99 6.5.2.2p10 / 6.8.6.4）：三个后端都要对。
 *
 * 这条用例是给 C99 输出后端立的门。它以前把"聚合值"实现成**被调方帧里
 * 那个临时对象的地址**，调用方拿到指针时那块内存已经随着 `ret` 失效：
 * `--emit-c` 出来的 C 在 `gcc -O0` 下碰巧能读对（栈还没被覆盖），`-O1`
 * 起稳定读到被复用的栈——`struct S s = f();` 的成员全是垃圾。native 和
 * LLVM 后端各有自洽的约定，所以这个问题只在 C99 后端露头。
 *
 * 现在 C99 后端改走"调用者给槽"：这种函数多收一个隐藏形参（结果槽地址），
 * `return <聚合>` 往里拷、再把这个指针返回。只开在 C99 后端，因此同一个
 * 程序里每一对调用点/定义都一致（见 src/parse.c6 的 CUR_SRET）。
 *
 * 覆盖：小结构体（8/12/16 字节）、带数组的大结构体（> 16 字节）、连续调用
 * （`addp(p, mk(…))`）、循环里反复调（被调方帧被复用）、递归返回聚合、
 * 作为实参传出去、以及按值返回后再取成员。 */
#include <stdio.h>
#include <string.h>

struct P { int x; int y; };
struct Big { long a[4]; };
struct Nine { char c[9]; };

static struct P mk(int x)
{
	struct P p;

	p.x = x;
	p.y = x + 1;
	return p;
}

static struct Big mkbig(long s)
{
	struct Big b;
	int i;

	for (i = 0; i < 4; i++)
		b.a[i] = s + i;
	return b;
}

static struct Nine mknine(int x)
{
	struct Nine n;
	int i;

	for (i = 0; i < 9; i++)
		n.c[i] = (char)(x + i);
	return n;
}

static int sump(struct P p)
{
	return p.x + p.y;
}

static struct P addp(struct P a, struct P b)
{
	struct P r;

	r.x = a.x + b.x;
	r.y = a.y + b.y;
	return r;
}

/* 返回聚合后立刻当实参再传（两层嵌套调用） */
static int chain(int n)
{
	struct P p = mk(n);

	return sump(addp(p, mk(10)));
}

/* 循环里反复调：被调方帧被反复复用，最容易踩到"返回地址已失效" */
static int loop_sum(int n)
{
	int t = 0, i;

	for (i = 0; i < n; i++) {
		struct P p = mk(i);

		t += p.x + p.y;
	}
	return t;
}

/* 递归里返回聚合 */
static struct P rec(int n)
{
	struct P p;

	if (n <= 0)
		return mk(1);
	p = rec(n - 1);
	p.x += 1;
	return p;
}


/* 零形参函数返回聚合（`struct P mk(void)`）：这条以前在 C99 后端失效——
 * c6c 把 `(void)`/`()` 这种无形参表当成"可以多传实参"，聚合返回的隐藏实参
 * 就被排除了；再加上"拿 vreg 0 当没结果槽"的哨兵，零形参函数的结果槽正好是
 * vreg 0，于是悄悄退回"返回被调方帧地址"，`gcc -O1` 下读出来是垃圾。 */
static struct P mk_zero(void)
{
	struct P p;

	p.x = 21;
	p.y = 22;
	return p;
}

static struct P mk_empty()
{
	struct P p = {23, 24};

	return p;
}

static int zero_arg_agg(void)
{
	struct P a = mk_zero();
	struct P b = mk_empty();
	int bad = 0;

	if (a.x != 21 || a.y != 22)
		bad++;
	if (b.x != 23 || b.y != 24)
		bad++;
	return bad;
}

int main(void)
{
	struct P a = mk(3);
	struct Big b = mkbig(100);
	struct Nine n = mknine(1);
	struct P c = addp(a, mk(1));
	struct P r = rec(3);
	int bad = 0;

	if (a.x != 3 || a.y != 4)
		bad++;
	if (b.a[0] != 100 || b.a[3] != 103)
		bad++;
	if (n.c[0] != 1 || n.c[8] != 9)
		bad++;
	if (c.x != 4 || c.y != 6)
		bad++;
	if (chain(5) != (5 + 6) + (10 + 11))
		bad++;
	if (loop_sum(4) != (0 + 1) + (1 + 2) + (2 + 3) + (3 + 4))
		bad++;
	if (r.x != 4 || r.y != 2)
		bad++;
	/* 再走一遍"取成员前先过一次函数调用"（以前最容易在这里读到脏值） */
	if (mk(9).y != 10)
		bad++;
	/* 零形参函数返回聚合（`mk_zero(void)` / `mk_empty()`） */
	bad += zero_arg_agg();
	printf("agg ok=%d bad=%d\n", bad == 0, bad);
	return bad != 0;
}
