/* 元组的 ABI 兼容性（c6 侧）
 *
 * 同一个函数，在 c6 里用元组声明、在纯 C 里用等价的 struct 声明，两边
 * 必须能互相调用：元组就是等价的 struct，实参和返回值都走同一套 ABI。
 *
 * tests/run-tests.sh 把本文件用 -std=c6 编译，把同目录的 tuple_plain.c
 * 用纯 C 编译（不带 -std=c6），再链接起来运行。
 */
// EXIT 0

/* 定义在纯 C 的翻译单元里，那边用 struct 返回 */
(int, int) plain_divmod(int a, int b);

/* 定义在本翻译单元，纯 C 侧按 struct 调用 */
(int, int) c6_divmod(int a, int b)
{
    return {a / b, a % b};
}

/* 纯 C 侧调用的检查入口：返回 0 表示全部通过 */
int c6_check(void)
{
    (int q, int r) = plain_divmod(17, 5);

    if (q != 3 || r != 2)
        return 1;
    {
        (int a, int b) = c6_divmod(9, 4);

        if (a != 2 || b != 1)
            return 2;
    }
    return 0;
}
