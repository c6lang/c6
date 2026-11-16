/* ctu 后端验收的"纯 C 侧"：提供 main 和一个被 c6 TU 调用的 C 函数。
 *
 * 这一侧的 argv 没有任何 c6c 产物参与：它证明 c6 TU 打出来的 .c 走的是**真
 * C ABI**（函数名、形参、返回值都是普通 C），能和别的 C 目标文件直接链。
 */

#include <stdio.h>

int a_run(int n);
int b_swap_add(int *p, int *q);

int c_scale(int x)
{
    return x * 3;
}

int main(void)
{
    int x = 4;
    int y = 9;
    int d = 0;

    printf("a_run(6)=%d\n", a_run(6));
    d = b_swap_add(&x, &y);
    printf("swap: d=%d x=%d y=%d\n", d, x, y);
    return 0;
}
