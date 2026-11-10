/* 泛型的 ABI 兼容性（纯 C 侧）
 *
 * 本翻译单元故意不加 -std=c6：'Vect@int' 的实例就是 'struct Vect_int'，
 * 'Pair@(int, int)' 就是 'struct Pair_int_int'，方法 'vect_sum' 的符号就是
 * 'Vect_int_vect_sum'。这里全部用普通 C 写，验证实例名、字段布局、方法符号
 * 和调用约定都是跨 TU 稳定的约定。
 */
struct Vect_int { int *data; int len; };
struct Pair_int_int { int a, b; };
struct Buf_int_4 { int data[4]; int len; };

void c6_fill(struct Vect_int *v, int *d, int n);      /* c6 侧用 Vect@int */
int Vect_int_vect_sum(struct Vect_int *v);            /* c6 侧的方法 */
int Vect_int_isum(struct Vect_int *v);                /* c6 侧的体内方法 */
struct Pair_int_int c6_pair(int a, int b);            /* c6 侧用 Pair@(int,int) */
int c6_check(void);
void c6_buf_fill(struct Buf_int_4 *b);
int plain_buf_check(void);

/* 本 TU 用普通 struct 定义，c6 侧按 Vect@int 调用 */
void plain_fill(struct Vect_int *v, int *d, int n)
{
    v->data = d;
    v->len = n;
}

struct Pair_int_int plain_pair(int a, int b)
{
    struct Pair_int_int p;

    p.a = a;
    p.b = b;
    return p;
}

int plain_check(void)
{
    int d[2] = {4, 5};
    struct Vect_int v;
    struct Pair_int_int p = c6_pair(7, 8);

    c6_fill(&v, d, 2);
    if (v.len != 2 || v.data[1] != 5)
        return 1;
    if (Vect_int_vect_sum(&v) != 9)
        return 2;
    if (Vect_int_isum(&v) != 9)
        return 4;
    if (p.a != 7 || p.b != 8)
        return 3;
    return 0;
}

void plain_buf_fill(struct Buf_int_4 *b)
{
    int i = 0;

    for (i = 0; i < 4; i++)
        b->data[i] = (i + 1) * 10;
    b->len = 4;
}

int plain_buf_check(void)
{
    struct Buf_int_4 b;

    if (sizeof b != 4 * sizeof(int) + sizeof(int))
        return 1;
    c6_buf_fill(&b);
    if (b.len != 4 || b.data[0] != 1 || b.data[3] != 4)
        return 2;
    return 0;
}

int main(void)
{
    int d[2] = {7, 8};
    struct Vect_int v;
    struct Pair_int_int p = c6_pair(1, 2);

    c6_fill(&v, d, 2);
    if (v.data[0] != 7 || v.len != 2)
        return 1;
    if (Vect_int_vect_sum(&v) != 15)
        return 2;
    if (p.a != 1 || p.b != 2)
        return 3;
    if (c6_check() != 0)
        return 4;
    if (plain_buf_check() != 0)
        return 5;
    return 0;
}
