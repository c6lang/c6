/* c6 'foreach |i, v| a { ... }' 示例
 *
 *   ./tcc/tcc -B tcc -std=c6 -run examples/foreach_demo.c
 *
 * 可迭代对象只有三种：完整数组、切片 'T[:]'、整数区间 'lo:hi'（半开）。
 * 下标绑定就是循环变量（size_t），值绑定是元素的一份副本。
 */
#include <stdio.h>

struct Pair { int a, b; };

static int sum(int[:] s)
{
    int total = 0;
    foreach |i, v| s {
        (void)i;
        total += v;
    }
    return total;
}

static void show_pairs(struct Pair[:] ps)
{
    foreach |i, p| ps {
        printf("ps[%d] = {%d, %d}\n", (int)i, p.a, p.b);
    }
}

int main(void)
{
    int a[5] = {10, 20, 30, 40, 50};
    struct Pair pairs[3] = {{1, 2}, {3, 4}, {5, 6}};
    int total = 0;

    /* 数组：下标 + 元素副本 */
    foreach |i, v| a {
        printf("a[%d] = %d\n", (int)i, v);
    }

    /* 切片：可迭代对象只求值一次，循环体里改数组不影响迭代次数 */
    printf("sum(a[1:4]) = %d\n", sum(a[1:4]));

    /* 区间 [0, n)：只有一个绑定 */
    foreach |i| 0:3 {
        printf("range %d\n", i);
    }

    /* '_' 丢弃绑定：重复两次 */
    foreach |_| 0:2 {
        printf("again\n");
    }

    /* 元素是结构体时也是副本：写 p 不改 pairs */
    show_pairs(pairs);
    foreach |i, p| pairs {
        (void)i;
        p.a = 0;
    }
    show_pairs(pairs);

    /* 值绑定可写，不影响容器 */
    foreach |i, v| a {
        (void)i;
        v *= 2;
        total += v;
    }
    printf("total = %d, a[0] = %d\n", total, a[0]);
    return 0;
}
