/* expect: 42 */
/* `typedef T name[]` + 带 const 的对象：限定符按 C11 6.7.3p9 落到元素上，
 * 元素类型变成 `const T`，和成员名表里登记的那个无限定 id 不是同一个。
 * 内核 include/linux/parser.h 的 `typedef struct match_token match_table_t[]`
 * 和 kernel/cpu.c 的 `static const match_table_t global_mitigations` 是这个形状。 */
struct mt {
    int token;
    int pattern;
};

typedef struct mt table_t[];

const table_t g = { {1, 10}, {2, 20} };

static const struct mt s[] = { {7, 70} };

static int peek(const table_t t, int i)
{
    return t[i].pattern;
}

int main(void)
{
    const struct mt *p = &g[1];

    if (g[0].token != 1)
        return 1;
    if (g[1].pattern != 20)
        return 2;
    if (p->token != 2)
        return 3;
    if (s[0].pattern != 70)
        return 4;
    if (peek(g, 0) != 10)
        return 5;
    return 42;
}
