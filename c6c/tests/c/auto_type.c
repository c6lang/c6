/* expect: 42 */
/* `__auto_type`（GNU）：类型由初始化器决定。
 *
 * 内核 include/linux/compiler_types.h 有一句 `# define auto __auto_type`，
 * 于是 minmax.h / cleanup.h / arch/x86/include/asm/bug.h 里所有
 * `auto ux = (x);` 都落到它上面。这里钉住三种推导（int / double / 指针）
 * 和"表达式只求值一次"——若实现成"解析两遍"，下面 side() 会被调两次。
 */
int printf(const char *fmt, ...);

/* 内核里的拼写来自 compiler_types.h 的这一行：
 *     # define auto __auto_type
 * 用例不 include 内核头，自己补上同一个定义。 */
#define auto __auto_type

static int nside;

static int side(void)
{
    nside++;
    return 7;
}

int main(void)
{
    auto n = 42;
    auto d = 3.5;
    auto p = "str";
    auto f = side();

    if (n != 42 || (int)sizeof(n) != 4) {
        printf("FAIL int deduction\n");
        return 1;
    }
    if (d != 3.5 || (int)sizeof(d) != 8) {
        printf("FAIL double deduction\n");
        return 1;
    }
    if (p[0] != 's' || p[2] != 'r') {
        printf("FAIL pointer deduction\n");
        return 1;
    }
    if (f != 7 || nside != 1) {
        printf("FAIL evaluated %d times\n", nside);
        return 1;
    }
    return 42;
}
