/* c6 编译期诊断：泛型 'struct Vect@T' / 'Vect@int'
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 * `// FLAGS` 行给本文件加上只管告警的开关：深度告警是风格提示，默认关闭。
 */
// FLAGS -Wgeneric-depth
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
// EXPECT test_instance_name_taken: error: the instance name 'Vect_int' is already in use
// EXPECT test_two_instances: error: cannot convert 'struct Vect_char' to 'struct Vect_int'
// EXPECT test_bad_kind: error: redefinition of template 'Vect' as a different kind
// EXPECT test_param_before_use: error: type parameter 'T' is used before it is declared: write '|T| T vat(...)'
// EXPECT test_same_name: error: a type parameter cannot have the same name as its template
// EXPECT test_dup_param: error: duplicate type parameter 'T'
// EXPECT test_unbound_param: error: 'T' undeclared (a type parameter is only visible inside its template)
// EXPECT test_fn_bare_name: error: 'id' is generic: write 'id@...'
// EXPECT test_value_binder_name: error: a value parameter is written '<type> NAME'
// EXPECT test_chain_depth: warning: generic instantiation nested 3 deep
// EXPECT test_meth_in_struct: error: a function cannot be defined inside a struct: write 'f' outside it
// EXPECT test_meth_no_body: error: a member function needs a body: write 'at(...) { ... }' in the template
// EXPECT test_too_many_params: error: too many type parameters
// EXPECT test_nested_too_deep: error: generic instantiation nested too deeply (max 64)

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
#elif defined test_instance_name_taken
struct Vect_int { long x; };
int main(void)
{
    Vect@int v = {0, 0};
    return v.x;
}
#elif defined test_two_instances
int main(void)
{
    Vect@int a = {0, 0};
    Vect@char b = {0, 0};
    a = b;
    return a.x;
}
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
#elif defined test_chain_depth
/* 'Vect@Vect@Vect@int' 写起来太深：-Wgeneric-depth 提示起个别名（§7）。
   这个片段同时带一个真错误，因为诊断组要求该片段编译失败。 */
typedef Vect@int IntVect;
int main(void)
{
    Vect@Vect@int two = {{1, 2}, {3, 4}};
    Vect@Vect@Vect@int deep = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
    IntVect alias = {9, 10};

    return two.x.x + deep.x.y.y + alias.x + missing_name;
}
#elif defined test_meth_in_struct
struct Plain { int a; int f(void) { return a; } };
int main(void) { return 0; }
#elif defined test_meth_no_body
struct Vec@T { T *d; T at(int i); };
int main(void) { Vec@int v = {0}; return v.d ? 0 : 1; }
#elif defined test_too_many_params
/* 形参最多 16 个（括号里或单个） */
struct Wide@(A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q) { A a; };
int main(void) { return 0; }
#elif defined test_nested_too_deep
/* 实例化嵌套最多 64 层：66 个 'Nest@' 就超了 */
struct Nest@T { T x; };
Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@Nest@int g;
int main(void) { return 0; }
#else
int main(void)
{
    Vect@int v = {3, 4};
    return (v.x + v.y) == 7 ? 0 : 1;
}
#endif
