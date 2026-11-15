/* 没写长度的数组（`T a[] = {…}`）：c6c 以前按 1 KB 估空间、超了就静默截断，
 * 于是 `sizeof(a)` 比 gcc 算出来的小一大截（tcc 的 asm_instrs[] 354 条只剩
 * 122 条就是这么来的）。这里把几种"数不全"的写法都摆出来，gcc 的输出当 oracle。
 *
 * 长度按 C11 6.7.8 的游标语义数：设计符把游标跳到 N+1，顺序值接在游标后面，
 * 嵌套花括号算一个条目（`{{1,2},{3,4}}` 是两个元素），花括号省略接着填。 */
#include <stdio.h>

/* 1. 范围设计符：一个条目就铺满 1000 个元素 */
static int range[] = {[0 ... 999] = 7};

/* 2. 范围设计符 + 结构体元素 */
struct triple {
    int a, b, c;
};
static struct triple tri[] = {[0 ... 399] = {1, 2, 3}};

/* 3. 设计符和顺序值混写：`[0 ... 2]` 铺 3 个，后面两个顺序值接在游标后面 */
static char mixed[][2][3] = {[0 ... 2] = {{3, 4, 5}, {6, 7, 8}}, {{9}, 10}, "abc"};

/* 4. 花括号省略：`{ {1}, 2 }` 在 gcc 眼里是**两个**元素（第一个只填了 a[0]） */
struct ss {
    char a[3], b;
};
static struct ss elide[] = {{1}, 2};

/* 5. 普通的顺序初始化 + 结尾多一个逗号 */
static short seq[] = {10, 20, 30, 40,};

/* 6. 字符串初始化，长度由字符串定（+1 是结尾的 0） */
static char str[] = "hello";

/* 7. 局部变量走的是同一条路 */
int main(void)
{
    int lrange[] = {[0 ... 49] = 3};
    char lstr[] = "abcd";
    int sum = 0;
    int i = 0;

    printf("range: %d %d %d\n", (int)(sizeof(range) / sizeof(range[0])), range[0], range[999]);
    printf("tri: %d %d %d %d\n", (int)(sizeof(tri) / sizeof(tri[0])), tri[0].a, tri[399].b, tri[399].c);
    printf("mixed: %d %d %d %d\n", (int)sizeof(mixed), mixed[0][0][0], mixed[3][0][0], mixed[4][2][0]);
    printf("elide: %d %d %d\n", (int)sizeof(elide), elide[1].a[0], elide[1].b);
    printf("seq: %d %d\n", (int)(sizeof(seq) / sizeof(seq[0])), seq[3]);
    printf("str: %d %s\n", (int)sizeof(str), str);
    printf("local: %d %d %s\n", (int)(sizeof(lrange) / sizeof(lrange[0])), lrange[49], lstr);

    for (i = 0; i < (int)(sizeof(range) / sizeof(range[0])); i++)
        sum += range[i];
    printf("sum: %d\n", sum);
    return 0;
}
