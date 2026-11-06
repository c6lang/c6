/* superc 切片 'T[:]' 示例
 *
 *   ./tcc/tcc -B tcc -std=tcc-ext -run examples/slice_demo.c
 *
 * 切片是「视图」：{元素指针, 长度} 两个机器字，不拥有元素、不扩容。
 */
#include <stdio.h>

/* 切片按值传递：头和长度被复制，元素仍然是同一块内存 */
static int sum(int[:] s)
{
    int i = 0, total = 0;
    for (i = 0; i < (int)s.len; i++)
        total += s[i];
    return total;
}

/* 切片可以作为返回值；对切片再切片得到新的视图 */
static int[:] drop_first(int[:] s)
{
    return s[1:];
}

struct Pair { int a, b; };
typedef int Row[3];                 /* 元素本身是数组时，先给类型起名字 */

int main(void)
{
    int a[5] = { 10, 20, 30, 40, 50 };
    int[:] s = a;                   /* 数组隐式转成切片，len 取数组长度 */

    printf("len=%d s[0]=%d s[4]=%d\n", (int)s.len, s[0], s[4]);

    /* 下标是带边界检查的：i >= len 时程序会被陷阱终止 */
    s[0] = 99;
    a[1] = 21;                      /* 反过来写数组，切片也看得到 */
    printf("a[0]=%d s[1]=%d\n", a[0], s[1]);

    /* 区间 [lo:hi)，三种省略写法 */
    int[:] mid = s[1:4];
    int[:] rest = s[2:];
    int[:] head = s[:2];
    printf("mid=%d,%d rest=%d head=%d\n",
           (int)mid.len, mid[0], (int)rest.len, (int)head.len);

    /* 对切片再切片、切片进函数、切片出函数 */
    int[:] t = drop_first(s);
    printf("sum=%d dropped=%d,%d\n", sum(s), (int)t.len, t[0]);

    /* .data 是不做边界检查的转义出口，.len 是长度 */
    printf("last=%d\n", s.data[(int)s.len - 1]);

    /* 元素可以是结构体、字符（字符串字面量自带结尾 NUL）、数组 */
    struct Pair pairs[2] = { {1, 2}, {3, 4} };
    struct Pair[:] ps = pairs;
    ps[1].a = 30;
    printf("pairs=%d,%d,%d\n", (int)ps.len, ps[0].a, ps[1].a);

    char[:] text = "hello";
    printf("text=%d,%c%c\n", (int)text.len, text[0], text[4]);

    int grid[2][3] = { {1, 2, 3}, {4, 5, 6} };
    Row[:] rows = grid;
    printf("rows=%d,%d,%d\n", (int)rows.len, rows[0][1], rows[1][2]);

    /* 手上有指针和长度时，用复合字面量构造切片 */
    int[:] view = (int[:]){ a + 2, 2 };
    printf("view=%d,%d,%d\n", (int)view.len, view[0], view[1]);

    /* 切片就是两个机器字 */
    printf("sizeof=%d\n", (int)sizeof(int[:]));
    return 0;
}
