/* c6 extension: diagnostics for slices 'T[:]' */
struct S;

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
