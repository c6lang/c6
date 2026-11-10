/* c6 编译期诊断：泛型 'struct Vect@T' / 'Vect@int'
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
struct Vect@T { T x, y; };
struct Fwd@T;
struct Bad@T { Bad@(T *) p; };

// EXPECT test_bare_name: error: 'Vect' is generic: write 'Vect@T'
// EXPECT test_arg_count: error: 'Vect' needs 1 type argument (got 2)
// EXPECT test_zero_arg: error: 'Vect' needs 1 type argument (got 0)
// EXPECT test_undefined: error: instantiation of undefined template 'Fwd'
// EXPECT test_recursive: error: cannot instantiate 'Bad' inside its own definition
// EXPECT test_redefine: error: redefinition of template 'Vect'
// EXPECT test_enum_param: error: enums cannot have type parameters
// EXPECT test_drop_struct: error: an instance drops 'struct'
// EXPECT test_value_param: error: value parameters are not implemented yet
// EXPECT test_bad_kind: error: redefinition of template 'Vect' as a different kind

#if defined test_bare_name
int main(void)
{
    Vect v = {1, 2};
    return v.x;
}
#elif defined test_arg_count
int main(void)
{
    Vect@(int, char) v = {1, 2};
    return v.x;
}
#elif defined test_zero_arg
int main(void)
{
    Vect@() v = {1, 2};
    return v.x;
}
#elif defined test_undefined
int main(void)
{
    Fwd@int f = {1};
    return f.x;
}
#elif defined test_recursive
int main(void)
{
    Bad@int b = {0};
    return b.p ? 1 : 0;
}
#elif defined test_redefine
struct Vect@T { T x, y; };
int main(void) { return 0; }
#elif defined test_enum_param
enum Color@T { Red, Green };
int main(void) { return 0; }
#elif defined test_drop_struct
int main(void)
{
    struct Vect@int v = {1, 2};
    return v.x;
}
#elif defined test_value_param
struct Buf@(int N) { int a[N]; };
int main(void)
{
    Buf@4 b = {{0}};
    return b.a[0];
}
#elif defined test_bad_kind
union Vect@T { T x; };
int main(void) { return 0; }
#else
int main(void)
{
    Vect@int v = {3, 4};
    return (v.x + v.y) == 7 ? 0 : 1;
}
#endif
