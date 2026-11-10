/* 元组的 ABI 兼容性（纯 C 侧）
 *
 * 本翻译单元故意是纯 C（`.c` 后缀）：它用普通的 struct 声明同一个函数，验证
 * 元组和 struct 的 ABI 完全一致（参数怎么传、返回值怎么回）。
 */
struct abi_pair { int q, r; };

struct abi_pair c6_divmod(int a, int b);   /* c6 侧用元组定义 */
int c6_check(void);                        /* c6 侧的检查入口 */

struct abi_pair plain_divmod(int a, int b) /* 本 TU 用 struct 定义 */
{
    struct abi_pair p;

    p.q = a / b;
    p.r = a % b;
    return p;
}

int main(void)
{
    struct abi_pair p = c6_divmod(17, 5);

    if (p.q != 3 || p.r != 2)
        return 1;
    if (c6_check() != 0)
        return 2;
    return 0;
}
