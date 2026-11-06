/* c6 编译期诊断：裸 tag 类型名（省略 struct / union / enum）
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过并运行成功。
 *
 * 裸 tag 只在「tag 可见 + 普通命名空间里没有同名符号」时成立；
 * 被变量/函数遮蔽时退回老 C 语义（要写 'struct'），未声明时也不会隐式前置声明。
 */
#include <sys/stat.h>       /* 'struct stat' 与函数 'stat()' 同名 */

struct S { int x; };
int S(void);                /* 普通命名空间里的 S：裸 'S' 不再表示类型 */

// EXPECT shadowed_by_function: error: ';' expected (got 'v')
// EXPECT shadowed_by_variable: error: ';' expected (got 'v')
// EXPECT posix_shadow: error: ';' expected (got 'st')
// EXPECT tag_not_declared: error: 'Nope' undeclared
// EXPECT tag_as_value: error: 'V' undeclared
// EXPECT tag_out_of_scope: error: 'Local' undeclared
// EXPECT incomplete_tag: error: initialization of incomplete type

#if defined shadowed_by_function
int main(void)
{
    S v = { 0 };
    return v.x;
}
#elif defined shadowed_by_variable
int main(void)
{
    int S = 0;
    S v = { 0 };
    return v.x;
}
#elif defined posix_shadow
int main(void)
{
    stat st = { 0 };
    return 0;
}
#elif defined tag_not_declared
int main(void)
{
    Nope *p = 0;
    return p != 0;
}
#elif defined tag_as_value
struct V { int a; };

int main(void)
{
    int x = V;
    return x;
}
#elif defined tag_out_of_scope
int main(void)
{
    {
        struct Local { int a; };
    }
    Local l = { 0 };
    return 0;
}
#elif defined incomplete_tag
struct Fwd;

int main(void)
{
    Fwd f = { 0 };
    return 0;
}
#else
/* 不带 -D 时的正常路径：裸 tag 照常用 */
struct Okay { int v; };
enum Tone { LOW = 1, HIGH = 2 };

int main(void)
{
    Okay o = { 7 };
    Tone t = HIGH;
    return o.v == 7 && t == HIGH ? 0 : 1;
}
#endif
