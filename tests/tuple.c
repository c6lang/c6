/* c6 自测：元组 '(T1, T2, ...)'
 *
 * 元组是值类型，布局与等价的 struct 完全相同，元素用常量下标 t[0]、t[1]
 * 访问，成员名 __c6_t0、__c6_t1 属于实现细节。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/tuple.c tests/tu/tuple.c
 */
#include <stdio.h>
#include <string.h>

struct Pair { int a, b; };

/* 文件作用域的元组变量，带初始化式 */
(int, int) g_pair = {3, 4};

/* ---- 返回多值 ---- */
(int, int) divmod(int a, int b)
{
    return {a / b, a % b};
}

(int, double) ratio(int a, int b)
{
    return {a / b, (double)a / b};
}

((int, int), int) pack(int a, int b)
{
    return {{a, b}, a + b};
}

(char *, int) greeting(void)
{
    return {"hello", 5};
}

/* ---- 元组作参数 ---- */
int sum((int, int) t)
{
    return t[0] + t[1];
}

void fill(struct Pair *p, (int, int) t)
{
    p->a = t[0];
    p->b = t[1];
}

/* ---- 跨翻译单元（tests/tu/tuple.c）---- */
int tu_pair_sum((int, int) t);
(int, int) tu_pair_make(int a);
int tu_pair_first((int, int) t);

/* ---- 测试块 ---- */

test "元组是值类型：布局、元素访问、元素赋值" {
    (int, int) t = {11, 22};

    if (t[0] != 11 || t[1] != 22)
        err(1, "元素访问不对");
    if ((int)sizeof(t) != 8 || (int)sizeof((int, int)) != 8)
        err(2, "sizeof 不是 8");
    t[0] = 1;
    t[1] = 2;
    if (t[0] != 1 || t[1] != 2)
        err(3, "元素赋值不对");
    if (g_pair[0] != 3 || g_pair[1] != 4)
        err(4, "文件作用域的元组变量不对");
}

test "返回多值：解构声明" {
    (int q, int r) = divmod(17, 5);

    if (q != 3 || r != 2)
        err(1, "divmod 解构不对");
    {
        (int a, int b) = divmod(9, 4);

        if (a != 2 || b != 1)
            err(2, "第二组解构不对");
        a += 10;                    /* 解构出来的就是普通局部变量 */
        if (a != 12)
            err(3, "解构出来的不是普通变量");
    }
}

test "返回多值：解构赋值" {
    int a = 0, b = 0;

    (a, b) = divmod(20, 6);
    if (a != 3 || b != 2)
        err(1, "解构赋值不对");
    (a, b) = divmod(7, 2);
    if (a != 3 || b != 1)
        err(2, "第二次解构赋值不对");
    {
        int arr[2] = {0, 0};

        (arr[0], arr[1]) = divmod(11, 3);
        if (arr[0] != 3 || arr[1] != 2)
            err(3, "数组元素作解构目标不对");
    }
    {
        struct Pair p = {0, 0};

        (p.a, p.b) = divmod(23, 7);
        if (p.a != 3 || p.b != 2)
            err(4, "成员作解构目标不对");
    }
}

test "'_' 占位符：只取需要的元素" {
    int a = 0, b = 0;

    (int q, _) = divmod(17, 5);
    if (q != 3)
        err(1, "只取第一个不对");
    (_, int r) = divmod(17, 5);
    if (r != 2)
        err(2, "只取第二个不对");
    (a, _) = divmod(20, 6);
    if (a != 3)
        err(3, "解构赋值里丢弃第二个不对");
    (_, b) = divmod(20, 6);
    if (b != 2)
        err(4, "解构赋值里丢弃第一个不对");
}

test "'_' 只在模式里特殊，别处仍是普通标识符" {
    int _ = 5;

    if (_ != 5)
        err(1, "'_' 作为普通变量不对");
    {
        (int, int) _ = divmod(8, 3);    /* 名叫 '_' 的元组变量 */

        if (_[0] != 2 || _[1] != 2)
            err(2, "名叫 '_' 的元组变量不对");
    }
}

test "元组按值传递、存进 struct、当数组元素" {
    (int, int) t = {4, 5};
    (int, int) a[3] = {{0, 0}, {0, 0}, {0, 0}};
    struct Holder { (int, int) t; char tag; };
    struct Holder h = {{0, 0}, 0};
    struct Pair p = {0, 0};
    int i = 0;

    if (sum(t) != 9)
        err(1, "元组作参数不对");
    fill(&p, t);
    if (p.a != 4 || p.b != 5)
        err(2, "从元组填 struct 不对");
    h.t = t;
    h.tag = 'x';
    if (h.t[0] != 4 || h.t[1] != 5 || h.tag != 'x')
        err(3, "元组作成员不对");
    for (i = 0; i < 3; i++) {
        a[i][0] = i;
        a[i][1] = i * 10;
    }
    if (a[2][0] != 2 || a[2][1] != 20 || (int)sizeof(a) != 24)
        err(4, "元组作数组元素不对");
}

test "混合元素与嵌套元组" {
    (int, double) r = ratio(7, 2);
    ((int, int), int) pk = pack(3, 4);
    (char *, int) g = greeting();

    if (r[0] != 3 || r[1] != 3.5)
        err(1, "混合元素不对");
    if ((int)sizeof(r) != 16)
        err(2, "混合元素的 sizeof 不对");
    if (pk[0][0] != 3 || pk[0][1] != 4 || pk[1] != 7)
        err(3, "嵌套元组不对");
    if (strcmp(g[0], "hello") != 0 || g[1] != 5)
        err(4, "指针元素不对");
}

test "显式转换与复合字面量" {
    (int, int) t = (int, int){1, 2};
    int n = 0;

    if (t[0] != 1 || t[1] != 2)
        err(1, "复合字面量不对");
    n = ((int, int){7, 8})[1];
    if (n != 8)
        err(2, "括号里的复合字面量下标不对");
    t = (int, int){5, 6};
    if (t[0] != 5 || t[1] != 6)
        err(3, "复合字面量赋值不对");
}

test "元组跨翻译单元共享同一布局" {
    (int, int) t = {4, 5};

    if (tu_pair_sum(t) != 9)
        err(1, "另一个 TU 读元组参数不对");
    if (tu_pair_first(t) != 4)
        err(2, "另一个 TU 的元素访问不对");
    (int x, int y) = tu_pair_make(6);
    if (x != 6 || y != 12)
        err(3, "另一个 TU 返回的元组不对");
}
