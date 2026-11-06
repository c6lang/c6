/* c6 编译期诊断：元组 '(T1, T2, ...)'
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
struct Fwd;                             /* 不完整类型 */

(int, int) dm(int a, int b)
{
    return {a / b, a % b};
}

// EXPECT test_void_element: error: 'tuple' element cannot be 'void'
// EXPECT test_function_element: error: 'tuple' element cannot be a function type
// EXPECT test_incomplete_element: error: 'tuple' requires a complete element type, not 'struct Fwd'
// EXPECT test_local_element: error: the element type of a tuple must be declared at file scope, but 'struct Local' is local
// EXPECT test_error_element: error: 'error' can only be a return type, not a tuple element
// EXPECT test_index_not_constant: error: constant expression expected
// EXPECT test_index_out_of_range: error: tuple index 2 is out of range (0..1)
// EXPECT test_arity_mismatch: error: the tuple has 2 elements but the pattern has 3
// EXPECT test_not_a_tuple: error: destructuring requires a tuple value
// EXPECT test_all_discard: error: 'tuple' discards every element
// EXPECT test_not_lvalue: error: lvalue expected
// EXPECT test_return_brace: error: 'return {…}' requires a struct, union or tuple return type

#if defined test_void_element
(int, void) bad(void);
int main(void) { return 0; }
#elif defined test_function_element
int main(void)
{
    (int, int (void)) t;
    return 0;
}
#elif defined test_incomplete_element
(int, struct Fwd) bad(void);
int main(void) { return 0; }
#elif defined test_local_element
int main(void)
{
    struct Local { int a; };
    (int, struct Local) t;
    return 0;
}
#elif defined test_error_element
(int, error) bad(void);
int main(void) { return 0; }
#elif defined test_index_not_constant
int main(void)
{
    (int, int) t = {1, 2};
    int i = 1;
    return t[i];
}
#elif defined test_index_out_of_range
int main(void)
{
    (int, int) t = {1, 2};
    return t[2];
}
#elif defined test_arity_mismatch
int main(void)
{
    (int a, int b, int c) = dm(9, 4);
    return a + b + c;
}
#elif defined test_not_a_tuple
int main(void)
{
    (int a, int b) = 5;
    return a + b;
}
#elif defined test_all_discard
int main(void)
{
    (_, _) = dm(9, 4);
    return 0;
}
#elif defined test_not_lvalue
int main(void)
{
    int i = 0;
    (i, 5) = dm(9, 4);
    return i;
}
#elif defined test_return_brace
int bad(void)
{
    return {1};
}
int main(void) { return bad(); }
#else
int main(void)
{
    (int q, int r) = dm(17, 5);
    int a = 0, b = 0;

    (a, b) = dm(9, 4);
    return q == 3 && r == 2 && a == 2 && b == 1 ? 0 : 1;
}
#endif
