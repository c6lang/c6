/* `__attribute__((cleanup(fn)))` 的出口边界。gcc 也支持这条属性，所以它的输出
 * 就是 oracle。
 *
 * 三处容易漏的语义（tcc tests2/101_cleanup 逐个逼出来的）：
 *   1. 属性写在**说明符**上时，这条声明的每个声明符都要清；写在声明符自己身上
 *      的只作用于那一个。
 *   2. `for (T v = …; …)` 的 init 声明属于 for 自己的作用域：循环正常结束要清，
 *      `break` 要清，`continue` **不**清（它还在这一轮里）。
 *   3. `continue` 的目标是外层循环：中间夹着的 `switch` 不是它的目标，但
 *      switch 和循环体那两层的 cleanup 都要跑到。 */
#include <stdio.h>

static int step;

static void done_int(int *p)
{
    printf("cleanup int %d at step %d\n", *p, step);
    step++;
}

static void done_str(char **p)
{
    printf("cleanup str %s at step %d\n", *p, step);
    step++;
}

static void one(void)
{
    __attribute__((cleanup(done_int))) int a = 1, b = 2;

    (void)a;
    (void)b;
    printf("one: body\n");
}

static void two(void)
{
    char *name __attribute__((cleanup(done_str))) = "x";

    (void)name;
    printf("two: body\n");
}

static void loops(void)
{
    int i = 0;

    step = 0;
    for (__attribute__((cleanup(done_int))) int fi = 10; i < 3; i++) {
        if (i == 1)
            continue;                 /* fi 还是活的，不清 */
        if (i == 2)
            break;                    /* 跳出循环：fi 要清 */
        printf("loops: i=%d fi=%d\n", i, fi);
    }
    printf("loops: after for\n");

    step = 0;
    i = 0;
    while (i < 3) {
        __attribute__((cleanup(done_int))) int w = 100 + i;

        printf("loops: w=%d\n", w);
        if (i == 0) {
            i++;
            continue;                 /* 清 w，然后下一轮 */
        }
        i++;
    }
    printf("loops: after while\n");
}

static void sw(int n)
{
    int i = 0;

    step = 0;
    for (i = 0; i < 2; i++) {
        __attribute__((cleanup(done_int))) int body = 20 + i;

        printf("sw: body=%d\n", body);
        switch (n) {
        case 0: {
            __attribute__((cleanup(done_int))) int inner = 30;

            printf("sw: inner=%d\n", inner);
            continue;                 /* switch 不是目标，两层的 cleanup 都跑 */
        }
        default:
            printf("sw: default\n");
            break;
        }
    }
    printf("sw: after for\n");
}

int main(void)
{
    step = 0;
    one();
    two();
    loops();
    sw(0);
    sw(1);
    printf("main: end at step %d\n", step);
    return 0;
}
