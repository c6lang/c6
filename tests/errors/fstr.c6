/* c6 编译期诊断：字符串插值 f"..{expr}.."
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
// EXPECT empty_braces: error: f-string: '{}' needs an expression
// EXPECT unterminated: error: f-string: missing '}'
// EXPECT fn_value: error: cannot interpolate a value of type
// EXPECT struct_value: error: cannot interpolate a value of type 'struct S'
// EXPECT int_slice: error: cannot interpolate 'int[:]': only a character slice holds text
// EXPECT void_value: error: cannot interpolate a value of type 'void'
// EXPECT global_plain: error: an f-string is not a constant expression
// EXPECT global_managed: error: an f-string is not a constant expression
// EXPECT borrow_to_managed: error: cannot assign a 'char *' to '@char'
// EXPECT managed_to_plain: error: a new reference cannot be stored in 'const char *'
// EXPECT nested_semicolon: error: f-string: ';' is only allowed once, to start the format spec
// EXPECT bad_conv: error: f-string: unknown conversion 'z' in a format spec
// EXPECT float_bad_conv: error: f-string: a 'double' value takes no 'x' format spec
// EXPECT int_float_conv: error: f-string: a 'int' value takes no 'f' format spec
// EXPECT conv_type: error: f-string: a 'const char *' value takes no 'x' format spec
// EXPECT width_big: error: f-string: format width 2000 is too large
// EXPECT precision_big: error: f-string: format precision 2000 is too large
// EXPECT empty_spec: error: f-string: empty format spec in '{expr;spec}'
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
#elif defined fn_value
int helper(int x)
{
    return x;
}
int main(void)
{
    /* 函数本身不是值；函数指针才是（按地址输出） */
    const char *p = f"{helper}";
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
#elif defined nested_semicolon
int main(void)
{
    int n = 1;
    /* 语句表达式里的 ';' 会截断表达式：只允许一次 ';'，用来引入格式说明 */
    const char *p = f"{(n; n)}";
    return p[0];
}
#elif defined bad_conv
int main(void)
{
    int n = 1;
    const char *p = f"{n;z}";
    return p[0];
}
#elif defined float_bad_conv
int main(void)
{
    double x = 1.5;
    const char *p = f"{x;x}";           /* 浮点只收 f e g */
    return p[0];
}
#elif defined int_float_conv
int main(void)
{
    int n = 1;
    const char *p = f"{n;f}";           /* 整数没有 f */
    return p[0];
}
#elif defined conv_type
int main(void)
{
    const char *s = "x";
    const char *p = f"{s;x}";           /* 文本不能按整数格式化 */
    return p[0];
}
#elif defined width_big
int main(void)
{
    int n = 1;
    const char *p = f"{n;2000d}";
    return p[0];
}
#elif defined precision_big
int main(void)
{
    int n = 1;
    const char *p = f"{n;.2000f}";      /* 精度和宽度各占 10 位 */
    return p[0];
}
#elif defined empty_spec
int main(void)
{
    int n = 1;
    const char *p = f"{n;}";
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
