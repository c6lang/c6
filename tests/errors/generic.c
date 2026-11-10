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
// EXPECT test_value_arg_not_const: error: constant expression expected
// EXPECT test_value_arg_range: error: value argument 40000 does not fit in 'short'
// EXPECT test_value_param_type: error: a value parameter must be an integer type no wider than 'int'
// EXPECT test_value_arg_parens: error: 'Arr' takes a value argument here: write 'Arr@(...)'
// EXPECT test_bad_kind: error: redefinition of template 'Vect' as a different kind
// EXPECT test_param_before_use: error: type parameter 'T' is used before it is declared: write '|T| T vat(...)'
// EXPECT test_same_name: error: a type parameter cannot have the same name as its template
// EXPECT test_dup_param: error: duplicate type parameter 'T'
// EXPECT test_unbound_param: error: 'T' undeclared (a type parameter is only visible inside its template)
// EXPECT test_fn_bare_name: error: 'id' is generic: write 'id@...'
// EXPECT test_value_binder_name: error: a value parameter is written '<type> NAME'

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
#elif defined test_value_arg_not_const
struct Buf@(T, int N) { T a[N]; };
int main(void)
{
    int n = 4;
    Buf@(int, n) b = {{0}};
    return b.a[0];
}
#elif defined test_value_arg_range
struct Buf@(T, short N) { T a[N]; };
int main(void)
{
    Buf@(int, 40000) b = {{0}};
    return b.a[0];
}
#elif defined test_value_param_type
struct Buf@(T, long long N) { T a[1]; };
int main(void) { return 0; }
#elif defined test_value_arg_parens
struct Arr@(int N) { int d[N]; };
int main(void)
{
    Arr@4 a = {{0}};
    return a.d[0];
}
#elif defined test_bad_kind
union Vect@T { T x; };
int main(void) { return 0; }
#elif defined test_param_before_use
T vat(Vect@T *self, int i);
int main(void) { return 0; }
#elif defined test_same_name
|Vect| Vect bad(Vect@Vect *v);
int main(void) { return 0; }
#elif defined test_dup_param
|T, T| void dup(T a);
int main(void) { return 0; }
#elif defined test_unbound_param
void f(Vect@T *v) { }
int main(void) { return 0; }
#elif defined test_fn_bare_name
|T| T id(T x) { return x; }
int main(void) { return id(3); }
#elif defined test_value_binder_name
|int| void f(void);
int main(void) { return 0; }
#else
int main(void)
{
    Vect@int v = {3, 4};
    return (v.x + v.y) == 7 ? 0 : 1;
}
#endif
