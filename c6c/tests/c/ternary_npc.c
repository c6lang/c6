/* expect: 51 */
/* 三元式的类型合并（C11 6.5.15p6）：
 *   1) 一边是空指针常量时让位：`0 ? (long*)0 : (void*)0` 是 `long*`
 *   2) 指向不完整数组的指针要和完整的补全：`(int (*)[])0 : (int (*)[4])0`
 * （`0 ? (int volatile*)0 : (void const*)0` 那条**没法在这里查**：gcc 的结果是
 *   `void const volatile*`，`_Generic` 分支写 `void*` 不匹配；c6c 现在不把
 *   限定符放进类型 id，两个类型是同一个 id，所以一定匹配。等 P2-2b 之后再加。）
 * 每个判据对上加对应的分，错一个就少一分。 */
struct s { int i; };
struct s g = { 7 };

int main(void)
{
    int n = 0;
    struct s *p = &g;

    n += _Generic(0 ? (long *)0 : (void *)0, long *: 1, default: 0);
    n += _Generic(0 ? (int (*)[4])0 : (int (*)[])0,
                  int (*)[4]: 4, default: 0);
    n += _Generic(0 ? (int (*)[])0 : (int (*)[4])0,
                  int (*)[4]: 8, default: 0);
    n += 40;
    /* `(cond ? p : (void*)0)->i` 也要能取成员 */
    if ((1 ? p : (void *)0)->i == 7)
        n -= 2;
    return n;
}
