/* `alloca` 的运行期大小：`alloca(strlen(s) + 1)`、`alloca(n * 2)` 这类以前直接
 * 报"必须是编译期常量"（只实现了字面量那条路）。tcc 上游的 boundtest.c 就是
 * 拿它当标准用法。gcc 当 oracle。
 *
 * 这里只测"开出来的那块内存能正常用、和别的栈对象不打架"——alloca 的语义本来
 * 就是调用者自己管边界。 */
#include <stdio.h>
#include <string.h>
#include <alloca.h>

static int fill(char *p, int n, int seed)
{
    int i = 0;
    int sum = 0;

    for (i = 0; i < n; i++)
        p[i] = (char)((seed + i) & 0x7f);
    for (i = 0; i < n; i++)
        sum += p[i];
    return sum;
}

int main(void)
{
    char *demo = "bound checking demo";
    char *p = 0;
    char *q = 0;
    int n = 7;
    int i = 0;
    int sum = 0;

    /* 运行期大小 + 嵌套在别的调用里（boundtest 的写法） */
    p = alloca(16);
    strcpy(p, "12345678901234");
    printf("alloca16: %s\n", p);
    printf("nested: %s\n", strcpy(q = alloca(strlen(demo) + 1), demo));

    /* 运行期大小来自变量与表达式 */
    q = alloca((size_t)(n * 2));
    sum = fill(q, n * 2, 3);
    printf("expr: n=%d sum=%d first=%d last=%d\n", n, sum, (int)(unsigned char)q[0],
           (int)(unsigned char)q[n * 2 - 1]);

    /* 循环里连续开：内容互不干扰（每次都是从 rsp 往下开新的） */
    for (i = 1; i <= 4; i++) {
        char *t = alloca((size_t)(i * 4));

        fill(t, i * 4, i);
        printf("loop %d: sum=%d\n", i, fill(t, i * 4, i));
    }

    printf("still fine: %s\n", strcpy(q = alloca(strlen(demo) + 1), demo));
    return 0;
}
