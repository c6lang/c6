/* c6 编译期诊断：切片 'T[:]'
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-errors.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
struct S;

// EXPECT test_void_element: error: '[:]' cannot have a 'void' element type
// EXPECT test_incomplete_element: error: '[:]' requires a complete element type, not 'struct S'
// EXPECT test_local_element: error: the element type of '[:]' must be declared at file scope, but 'struct Local' is local
// EXPECT test_error_element: error: 'error' can only be a return type, not a slice element
// EXPECT test_element_mismatch: error: cannot make a slice of 'char' from an array of 'int'
// EXPECT test_pointer_source: error: cannot make a slice from a pointer; use '(T[:]){p, n}'
// EXPECT test_vla_source: error: cannot slice a variable length array
// EXPECT test_pointer_slice: error: cannot slice a pointer; use '(T[:]){p, n}'
// EXPECT test_capacity: error: slice bounds have no capacity
// EXPECT test_file_scope_array: error: a slice cannot be initialized from an array in a static initializer; use '{ data, len }'
// EXPECT test_uninitialized: error: variable 's' must be initialized
// EXPECT test_slice_to_int: error: cannot convert 'int[:]' to 'int'

#if defined test_void_element
void f(void[:] s);
int main(void) { return 0; }
#elif defined test_incomplete_element
void f(struct S[:] s);
int main(void) { return 0; }
#elif defined test_local_element
int main(void)
{
    struct Local { int a; };
    struct Local l = { 1 };
    struct Local[:] s = &l;
    return 0;
}
#elif defined test_error_element
void f(error[:] s);
int main(void) { return 0; }
#elif defined test_element_mismatch
int main(void)
{
    int a[2] = { 1, 2 };
    char[:] s = a;
    return 0;
}
#elif defined test_pointer_source
int main(void)
{
    int a[2] = { 1, 2 };
    int *p = a;
    int[:] s = p;
    return 0;
}
#elif defined test_vla_source
int main(void)
{
    int n = 3;
    int a[n];
    int[:] s = a;
    return 0;
}
#elif defined test_pointer_slice
int main(void)
{
    int a[2] = { 1, 2 };
    int *p = a;
    return p[0:1] != 0;
}
#elif defined test_capacity
int main(void)
{
    int a[2] = { 1, 2 };
    int[:] s = a;
    return (int)s[0:1:2].len;
}
#elif defined test_file_scope_array
int a[3] = { 1, 2, 3 };
int[:] g = a;
int main(void) { return 0; }
#elif defined test_uninitialized
int main(void)
{
    int[:] s;
    return 0;
}
#elif defined test_slice_to_int
int main(void)
{
    int a[2] = { 1, 2 };
    int[:] s = a;
    return (int)s;
}
#else
int main(void)
{
    int a[3] = { 1, 2, 3 };
    int[:] s = a[1:];
    return (int)s.len == 2 && s[0] == 2 ? 0 : 1;
}
#endif
