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
