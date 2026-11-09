/* c6 编译期诊断：'foreach |i, v| a { ... }'
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-tests.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过并运行成功。
 */
struct S;
extern int unknown[];

// EXPECT test_one_binder: error: an array or slice yields an index and a value: write 'foreach |i, v| a'
// EXPECT test_two_binders_range: error: a range yields only an index: write 'foreach |i| lo:hi'
// EXPECT test_both_discard: error: 'foreach' discards both binders
// EXPECT test_three_binders: error: 'foreach' takes at most two bindings
// EXPECT test_no_name: error: 'foreach' expects a binding name
// EXPECT test_pointer: error: cannot iterate over 'int *' (no 'has'/'next' iterator: use a slice parameter 'T[:]', or '(T[:]){p, n}')
// EXPECT test_int: error: cannot iterate over 'int' (write a range: 'foreach |i| lo:hi')
// EXPECT test_struct: error: cannot iterate over 'struct S' (no 'has'/'next' iterator: expose a slice member 'T[:] items', or define 'int has(); E next();')
// EXPECT test_struct_pointer: error: cannot iterate over 'struct S *' (no 'has'/'next' iterator: use a slice parameter 'T[:]', or '(T[:]){p, n}')
// EXPECT test_vla: error: cannot iterate over a variable length array
// EXPECT-C99 test_vla: error: c99 output: variable length arrays are not supported
// EXPECT test_unknown_length: error: cannot iterate over an array of unknown length
// EXPECT test_float_range: error: range bounds must be integers
// EXPECT test_no_block: error: '{' expected
// EXPECT test_array_element: error: 'foreach' cannot copy an element of type 'int [2]'
// EXPECT test_file_scope: error: 'foreach' can only be used inside a function

#if defined test_one_binder
int main(void)
{
    int a[2] = { 1, 2 };
    foreach |i| a {
        a[0] = i;
    }
    return a[0];
}
#elif defined test_two_binders_range
int main(void)
{
    int total = 0;
    foreach |i, v| 0:2 {
        total += i + v;
    }
    return total;
}
#elif defined test_both_discard
int main(void)
{
    int a[2] = { 1, 2 };
    foreach |_, _| a {
        a[0] = 0;
    }
    return a[0];
}
#elif defined test_three_binders
int main(void)
{
    int a[2] = { 1, 2 };
    foreach |i, v, w| a {
        a[0] = i + v + w;
    }
    return a[0];
}
#elif defined test_no_name
int main(void)
{
    int a[2] = { 1, 2 };
    foreach | | a {
        a[0] = 0;
    }
    return a[0];
}
#elif defined test_pointer
int main(void)
{
    int a[2] = { 1, 2 };
    int *p = a;
    foreach |i, v| p {
        a[0] = i + v;
    }
    return a[0];
}
#elif defined test_int
int main(void)
{
    int n = 2;
    foreach |i, v| n {
        n += i + v;
    }
    return n;
}
#elif defined test_struct
int main(void)
{
    struct S *ps = 0;
    foreach |i, v| *ps {
        ps = (struct S *)(long)(i + v);
    }
    return ps != 0;
}
#elif defined test_struct_pointer
int main(void)
{
    struct S *ps = 0;
    foreach |i, v| ps {
        ps = (struct S *)(long)(i + v);
    }
    return ps != 0;
}
#elif defined test_vla
int main()
{
    int n = 2;
    int a[n];
    foreach |i, v| a {
        a[0] = i + v;
    }
    return a[0];
}
#elif defined test_unknown_length
int main(void)
{
    foreach |i, v| unknown {
        return i + v;
    }
    return 0;
}
#elif defined test_float_range
int main(void)
{
    foreach |i| 0.0:2.0 {
        return i;
    }
    return 0;
}
#elif defined test_no_block
int main(void)
{
    int a[2] = { 1, 2 };
    foreach |i, v| a
        a[0] = i + v;
    return a[0];
}
#elif defined test_array_element
int main(void)
{
    int a[2][2] = { { 1, 2 }, { 3, 4 } };
    foreach |i, v| a {
        a[0][0] = i + v[0];
    }
    return a[0][0];
}
#elif defined test_file_scope
int a[2] = { 1, 2 };
foreach |i, v| a {
    a[0] = i + v;
}
#else
/* 正常路径：数组、切片、区间各走一遍 */
struct P { int x, y; };

static int sum(int[:] s)
{
    int total = 0;
    foreach |i, v| s {
        total += (int)i + v;
    }
    return total;
}

int main(void)
{
    int a[3] = { 1, 2, 3 };
    struct P ps[2] = { { 1, 2 }, { 3, 4 } };
    int total = 0;

    foreach |i, v| a {
        total += v;
    }
    foreach |i| 1:3 {
        total += i;
    }
    foreach |i, p| ps {
        total += (int)i + p.y;
    }
    if (total != 6 + 1 + 2 + 0 + 2 + 1 + 4)
        return 1;
    if (sum(a) != (0 + 1) + (1 + 2) + (2 + 3))
        return 2;
    return 0;
}
#endif
