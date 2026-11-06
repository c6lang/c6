/* tests/slice.c 的第二个翻译单元：切片 API。
 *
 * 两个 TU 各自写 'int[:]'，布局相同，不需要额外声明。
 */
int tu_sum(int[:] s)
{
    int i = 0, total = 0;
    for (i = 0; i < (int)s.len; i++)
        total += s[i];
    return total;
}

int[:] tu_tail(int[:] s)
{
    return s[1:];
}

int tu_first(int[:] s)
{
    return s.len > 0 ? s[0] : -1;
}
