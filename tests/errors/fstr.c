/* c6 编译期诊断：字符串插值 f"..{expr}.."
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
// EXPECT empty_braces: error: f-string: '{}' needs an expression
// EXPECT unterminated: error: f-string: missing '}'
// EXPECT double_value: error: cannot interpolate a value of type 'double'
// EXPECT struct_value: error: cannot interpolate a value of type 'struct S'
// EXPECT int_slice: error: cannot interpolate 'int[:]': only a character slice holds text
// EXPECT void_value: error: cannot interpolate a value of type 'void'
// EXPECT global_plain: error: an f-string is not a constant expression
// EXPECT global_managed: error: an f-string is not a constant expression
// EXPECT borrow_to_managed: error: cannot assign a 'char *' to '@char'
// EXPECT managed_to_plain: error: a new reference cannot be stored in 'const char *'
// EXPECT semicolon: error: f-string: ';' is not allowed in an interpolation
// EXPECT try_in_fstr: error: 'try' is not allowed in an interpolation
// EXPECT macro_param: error: 'n' undeclared

#if defined empty_braces
int main(void)
{
    const char *p = f"{}";
    return p[0];
}
#elif defined unterminated
int main(void)
{
    int n = 1;
    const char *p = f"a{n";
    return p[0];
}
#elif defined double_value
int main(void)
{
    const char *p = f"{1.5}";
    return p[0];
}
#elif defined struct_value
struct S { int x; };
int main(void)
{
    struct S s = {0};
    const char *p = f"{s}";
    return p[0];
}
#elif defined int_slice
int main(void)
{
    int a[2] = { 1, 2 };
    int[:] s = a;
    const char *p = f"{s}";
    return p[0];
}
#elif defined void_value
void nothing(void) { }
int main(void)
{
    const char *p = f"{nothing()}";
    return p[0];
}
#elif defined global_plain
const char *p = f"x";
int main(void)
{
    return 0;
}
#elif defined global_managed
@char p = @f"x";
int main(void)
{
    return 0;
}
#elif defined borrow_to_managed
int main(void)
{
    int n = 1;
    @char p = f"x{n}";
    return p[0];
}
#elif defined managed_to_plain
int main(void)
{
    int n = 1;
    const char *p = @f"x{n}";
    return p[0];
}
#elif defined semicolon
int main(void)
{
    int n = 1;
    const char *p = f"{n;d}";           /* ';' 不是分隔符，也不许出现在这里 */
    return p[0];
}
#elif defined try_in_fstr
int!error boom(int n)
{
    err(1, "boom");
    return n;
}
static int!error test(void)
{
    /* 插值里 try：错误路径会从函数返回，构造器就留在栈上了 */
    return (int)f"{(try boom(1))}";
}
int main(void)
{
    int v = test() catch e { return 0; };
    return v;
}
#elif defined macro_param
#define MACRO_PARAM(n) f"n{n}"
int main(void)
{
    /* 宏形参在 f".." 的插值里不展开（表达式是后来才过预处理的） */
    const char *p = MACRO_PARAM(1);
    return p[0];
}
#else
int main(void)
{
    int n = 1;
    const char *p = f"ok {n}";
    @char q = @f"ok {n}";

    if (p[0] != 'o' || q[0] != 'o')
        return 1;
    return 0;
}
#endif
