/* C99 输出后端：泛型实例就是普通 struct，输出里不剩下任何 '@'
 *
 * 'Vect@int' 展平成 'struct Vect_int'，所以生成的 C 源码和手写 mangle 的
 * 结果一样，gcc 能编、行为一致。
 *
 * 运行（仓库根目录）：
 *   make c99test
 */
// EXIT 0
// OUTPUT 6 30
// OUTPUT 4
// OUTPUT 8

#include <stdio.h>

struct Vect@T { T *data; int len; };

int vect_sum(Vect@int *v)
{
    int i = 0, s = 0;

    for (i = 0; i < v->len; i++)
        s += v->data[i];
    return s;
}

struct Pair@(A, B) { A a; B b; };

/* 值形参：实例名带上它的拼写，'Buf@(int, 4)' 就是 'struct Buf_int_4' */
struct Buf@(T, int N) { T data[N]; int len; };

int main(void)
{
    int d[3] = {1, 2, 3};
    long ld[2] = {10, 20};
    Vect@int v = {d, 3};
    Vect@long lv = {ld, 2};
    Pair@(int, char *) p = {7, "x"};
    Buf@(int, 4) b = {{1, 2, 3, 4}, 4};

    printf("%d %d\n", (&v)->vect_sum(), (int)(lv.data[0] + lv.data[1]));
    printf("%d\n", (int)sizeof(Vect@long) / (int)sizeof(p.a));
    printf("%d\n", b.data[3] + b.len);
    return 0;
}
