/* C99 输出后端：函数模板的实例就是普通函数
 *
 * '|T| T vat(Vect@T *, int)' 在实例化后是 'Vect_int_vat'，输出里既没有 '|'
 * 也没有 '@'——所以生成的 C 源码和手写 mangle 的 的结果一样，gcc 能编、行为
 * 一致。模板体在编译单元末尾排队编译，输出的是同一个普通函数。
 *
 * 运行（仓库根目录）：
 *   make c99test
 */
// EXIT 0
// OUTPUT 1 2 6
// OUTPUT id 7
// OUTPUT box q5

#include <stdio.h>

struct Vect@T { T *data; int len; };

|T| T vat(Vect@T *self, int i) { return self->data[i]; }
|T| int vsum(Vect@T *self)
{
    int i = 0, s = 0;

    for (i = 0; i < self->len; i++)
        s += (int)vat@T(self, i);
    return s;
}
|T| T id(T x) { return x; }
|T| typedef struct { T v; int n; } Box;

int main(void)
{
    int d[3] = {1, 2, 3};
    Vect@int v = {d, 3};
    Box@char b = {'q', 5};

    printf("%d %d %d\n", (&v)->vat(0), vat@int(&v, 1), (&v)->vsum());
    printf("id %d\n", id@int(7));
    printf("box %c%d\n", b.v, b.n);
    return 0;
}
