/* superc 编译期诊断：try / catch / T!error
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-errors.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
static int ok_int(void) { return 1; }
static int!error fails(int n) { if (n) err(1, "x"); return 0; }
struct S { int a; };

// EXPECT test_try_in_plain_fn: error: 'try' requires the enclosing function to return 'T!error'
// EXPECT test_try_not_fallible: error: 'try' requires a call to a function returning 'T!error'
// EXPECT test_try_not_a_call: error: 'try' requires a call to a function returning 'T!error'
// EXPECT test_try_assignment: error: 'try' requires a call to a function returning 'T!error'
// EXPECT test_catch_not_fallible: error: 'catch' requires a call to a function returning 'T!error'
// EXPECT test_catch_falls_through: error: 'catch' body must end with 'return', 'err()' or 'goto'
// EXPECT test_bare_catch: error: 'catch' has no operand
// EXPECT test_fallible_not_a_function: error: '!error' requires a function declarator
// EXPECT test_fallible_struct_return: error: '!error' requires a return type representable by -1
// EXPECT test_try_in_defer: error: 'try' is not allowed in a defer body
// EXPECT test_error_parameter: error: 'error' can only be a return type, not a parameter
// EXPECT test_error_out_parameter: error: 'error' can only be a return type, not a parameter
// EXPECT test_error_parameter_knr: error: 'error' can only be a return type, not a parameter
// EXPECT test_catch_in_defer: error: 'catch' is not allowed in a defer body

#if defined test_try_in_plain_fn
static int plain(void) { try fails(0); return 0; }
int main(void) { return plain(); }
#elif defined test_try_not_fallible
static int!error bad(void) { try ok_int(); return 0; }
int main(void) { return bad(); }
#elif defined test_try_not_a_call
static int!error bad(int n) { try n; return 0; }
int main(void) { return bad(1); }
#elif defined test_try_assignment
static int!error bad(void) { int v = 0; try v = 1; return v; }
int main(void) { return bad(); }
#elif defined test_catch_not_fallible
static int bad(void) { int v = ok_int() catch e { return -1; }; return v; }
int main(void) { return bad(); }
#elif defined test_catch_falls_through
static int!error bad(void) { int v = fails(0) catch e { v = 1; }; return v; }
int main(void) { return bad(); }
#elif defined test_bare_catch
static int bad(void) { catch e { return -1; } return 0; }
int main(void) { return bad(); }
#elif defined test_fallible_not_a_function
static int!error x = 1;
int main(void) { return x; }
#elif defined test_fallible_struct_return
static struct S!error bad_ret(void) { struct S s = {0}; return s; }
int main(void) { return bad_ret().a; }
#elif defined test_try_in_defer
static int!error bad(void) { defer { try fails(0); } return 0; }
int main(void) { return bad(); }
#elif defined test_error_parameter
static void take(error e) { }
int main(void) { return 0; }
#elif defined test_error_out_parameter
static void take(error *e) { }
int main(void) { return 0; }
#elif defined test_error_parameter_knr
static void take(e) error e; { }
int main(void) { return 0; }
#elif defined test_catch_in_defer
static int!error bad(void) { defer { int v = fails(0) catch e { return -1; }; } return 0; }
int main(void) { return bad(); }
#else
static int!error check(void)
{
    if ((try fails(0)) != 0)
        return 1;
    fails(1) catch e {
        return e.id == 1 ? 0 : 2;
    };
    return 3;
}

int main(void)
{
    int v = check() catch e {
        return -1;
    };
    return v;
}
#endif
