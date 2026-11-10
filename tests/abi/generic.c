/* 泛型的 ABI 兼容性（c6 侧）
 *
 * 'Vect@int' 的实例标签是 'Vect_int'，布局是普通 struct 的布局，方法的符号
 * 名就是 '<实例标签>_<方法名>'（'Vect_int_vect_sum'）。所以纯 C 的翻译单元
 * 只要写出 'struct Vect_int' 就能和 c6 侧互相调用——mangle 是跨 TU 的约定，
 * 不是编译器的私事。
 *
 * 注意实例按值传递的形态：c99 后端把 8 字节的 struct 打包成一个整型寄存器
 * 传，和 SysV 一致；更大的 struct 它用隐藏返回指针，和系统 ABI 不符（与泛型
 * 无关的既有后端限制）。所以这里按值过边界的只用 8 字节的 'Pair@(int,int)'，
 * 大的 'Vect@int' 一律走指针。
 *
 * tests/run-tests.sh 把本文件用 -std=c6 编译，把同目录的 generic_plain.c
 * 用纯 C 编译（不带 -std=c6），再链接起来运行。
 */
// EXIT 0

struct Vect@T { T *data; int len; };
struct Pair@(A, B) { A a; B b; };
struct Buf@(T, int N) { T data[N]; int len; };

/* 方法：符号名是 Vect_int_vect_sum */
int vect_sum(Vect@int *v)
{
    int i = 0, s = 0;

    for (i = 0; i < v->len; i++)
        s += v->data[i];
    return s;
}

/* 实例走指针 */
void c6_fill(Vect@int *v, int *d, int n)
{
    v->data = d;
    v->len = n;
}

/* 8 字节的实例按值返回：和普通 struct 走同一套 ABI */
Pair@(int, int) c6_pair(int a, int b)
{
    Pair@(int, int) p = {a, b};

    return p;
}

/* 值形参也是 mangle 的一部分：'Buf@(int, 4)' 就是 'struct Buf_int_4'，
   数组长度 'N' 在布局里已经是一个普通常量 */
void c6_buf_fill(Buf@(int, 4) *b)
{
    int i = 0;

    for (i = 0; i < 4; i++)
        b->data[i] = i + 1;
    b->len = 4;
}

/* 纯 C 侧用普通 struct 定义 */
void plain_fill(Vect@int *v, int *d, int n);
Pair@(int, int) plain_pair(int a, int b);
int plain_check(void);
void plain_buf_fill(struct Buf_int_4 *b);
int plain_buf_check(void);

int c6_check(void)
{
    int d[3] = {1, 2, 3};
    Vect@int v = {0, 0};
    Pair@(int, int) p = {0, 0};

    plain_fill(&v, d, 3);
    if (v.len != 3 || v.data[2] != 3)
        return 1;
    if ((&v)->vect_sum() != 6)
        return 2;
    p = plain_pair(4, 5);
    if (p.a != 4 || p.b != 5)
        return 3;
    if (plain_check() != 0)
        return 4;
    {
        Buf@(int, 4) b = {{0}, 0};

        plain_buf_fill(&b);
        if (b.len != 4 || b.data[3] != 40)
            return 5;
        c6_buf_fill(&b);
        if (b.len != 4 || b.data[3] != 4)
            return 6;
    }
    if (plain_buf_check() != 0)
        return 7;
    return 0;
}
