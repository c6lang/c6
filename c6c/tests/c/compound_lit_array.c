/* expect: 42 */
/* 数组类型的复合字面量：`(T[]){…}` / `(T[N]){…}` 落在全局指针、全局聚合的
 * 成员、局部指针、以及嵌套在别的花括号初始化器里都要给出**对象地址**
 * （以前指针槽里落的是第一个元素的值）。内核 include/linux/seq_buf.h:29 的
 * DECLARE_SEQ_BUF 就是 `.buffer = (char[SIZE]){ 0 }` 这个形状。 */
struct s {
	int *p;
	unsigned n;
};

struct s g1 = { .p = (int[]){ 7, 8 }, .n = 2 };
struct s g2[2] = { { (int[]){ 1, 2 }, 2 }, { (int[]){ 3, 4 }, 2 } };
static int *gp = (int[]){ 5, 6 };
static char *gc = (char[4]){ 0 };

#define DECLARE_SEQ_BUF(NAME, SIZE) \
	struct seq_buf NAME = { .buffer = (char[SIZE]) { 0 }, .size = SIZE }
struct seq_buf {
	char *buffer;
	unsigned size;
};
DECLARE_SEQ_BUF(sb, 8);

int main(void)
{
	int *lp = (int[]){ 9, 10 };
	struct s ls = { .p = (int[]){ 5, 12 }, .n = 1 };
	int r = 0;

	r += g1.p[1];                       /* 8 */
	r += g2[0].p[0] + g2[1].p[1];       /* 1 + 4 = 5 */
	r += gp[1];                         /* 6 */
	r += lp[1];                         /* 10 */
	r += ls.p[0];                       /* 5 */
	r += (int)sb.size;                  /* 8 */
	if (gc[3] != 0 || sb.buffer[7] != 0 || g1.n != 2 || ls.n != 1)
		return 1;
	if (g2[0].n != 2 || g2[1].n != 2)
		return 2;
	return r;
}
