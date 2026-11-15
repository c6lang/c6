/* expect: 123 */
/* 无名位域：不占初始化器的位置，也不抬结构体的对齐（gcc 的口径）。
 * 期望：{1,2,3} 落到 a/b/c，sizeof 是 28 —— a*100 + b*10 + c = 123；
 * sizeof 不对就 +100（退出码只有 8 位，别让期望值超 255）。 */
typedef struct {
    unsigned int a;
    unsigned int : 32;
    unsigned int b;
    unsigned long long : 64;
    unsigned int c;
} tst_bf;

tst_bf arr[1] = { { 1, 2, 3 } };

int main(void)
{
    int n = 0;

    n = (int)arr[0].a * 100 + (int)arr[0].b * 10 + (int)arr[0].c;
    if (sizeof(tst_bf) == 28)
        n += 0;
    else
        n += 100;
    return n;
}
