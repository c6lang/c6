/* 9--16 字节 struct 返回值（纯整数成员）的 ABI 兼容性（纯 C 侧）
 *
 * 本翻译单元故意是纯 C（`.c` 后缀）：它用普通 C 定义/调用同一批函数，验证
 * c6 侧（尤其是 -c99 后端生成的 C）和系统的 x86-64 ABI 一致。
 */
struct S9  { char a[9]; };
struct S12 { int a, b, c; };
struct S16 { int a, b, c, d; };

struct S9  c6_mk9(int x);
struct S12 c6_mk12(int x);
struct S16 c6_mk16(int x);
int c6_check(void);

struct S9 plain_mk9(int x)
{
    struct S9 s;
    int i;

    for (i = 0; i < 9; i++)
        s.a[i] = (char)(x + i);
    return s;
}

struct S12 plain_mk12(int x)
{
    struct S12 s;

    s.a = x;
    s.b = x + 1;
    s.c = x + 2;
    return s;
}

struct S16 plain_mk16(int x)
{
    struct S16 s;

    s.a = x;
    s.b = x + 1;
    s.c = x + 2;
    s.d = x + 3;
    return s;
}

struct S12 (*plain_fp12)(int) = plain_mk12;

int main(void)
{
    struct S12 a = c6_mk12(7);
    struct S16 b = c6_mk16(8);
    struct S9 c = c6_mk9(1);
    int i;

    if (a.a != 7 || a.b != 8 || a.c != 9)
        return 1;
    if (b.a != 8 || b.b != 9 || b.c != 10 || b.d != 11)
        return 2;
    for (i = 0; i < 9; i++)
        if (c.a[i] != 1 + i)
            return 3;
    return c6_check();
}
