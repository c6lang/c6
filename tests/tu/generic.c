/* tests/generic.c 的第二个翻译单元：泛型状态必须按 TU 复位
 *
 * 两个 TU 各自声明同一批模板、各自实例化：符号名（`Vect_int`、
 * `Vect_int_at`）与结构布局都由 mangle 决定，所以链接期自然合成一份。
 *
 * 曾经的 bug：每个 TU 结尾不清泛型表，下一个 TU 开头才去释放上一个 TU
 * 的模板体 token 串——那时预处理器的 token 池已经换过一轮，指针是悬空的，
 * 于是「TU1 里有模板 + TU2 随便什么」就段错误。`tests/generic.c` 一带上这个
 * 第二 TU 就会踩到。
 */
struct Vect@T {
    T *data;
    int len;
    T at(int i) { return data[i]; }
};

Vect@int tu_vec = {0, 0};
static int tu_data[3] = {3, 4, 5};

/* 由 tests/generic.c 调用；只用本 TU 自己的实例 */
int tu_gen_sum(void)
{
    Vect@int v = {tu_data, 3};
    Vect@int *p = &v;

    tu_vec = v;
    return p->at(0) + p->at(1) + p->at(2);
}
