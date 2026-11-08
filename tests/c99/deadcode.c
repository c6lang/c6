/* C99 输出后端：以 return 结束所有分支的函数，以及 return 之后的死代码。
 *
 * 后端过去在死代码里也生成 goto 标签：一来 tcc 前端据此认为函数体末尾
 * 可达，误报 "function might return no value"（用 -std=c6 编译 tcc 自己
 * 时就是那 13 个警告）；二来生成的 C 里留下没人跳转的标签，gcc 在
 * -Wall -Wextra -Werror 下会报 unused label，所以这个用例把两种形态都
 * 摆出来：if/else 两个分支都 return、while (1) 里 return、return 后还有
 * 语句。
 *
 * 运行（仓库根目录）：
 *   make c99test
 */

// EXIT 0

static int sign_of(int a, int b)
{
    if (a < 0) {
        return -1;
    } else if (b < 0) {
        return -2;
    } else {
        return 0;
    }
}

static int count_up(int from)
{
    while (1) {
        if (from >= 0)
            return from;
        from += 1;
    }
}

static int twice(int x)
{
    return x * 2;
    x = x + 1;              /* 死代码：不生成语句，也不留下标签 */
    return x;
}

int main(void)
{
    if (sign_of(-1, 2) != -1)
        return 1;
    if (sign_of(1, -2) != -2)
        return 2;
    if (sign_of(1, 2) != 0)
        return 3;
    if (count_up(-3) != 0)
        return 4;
    if (twice(21) != 42)
        return 5;
    return 0;
}
