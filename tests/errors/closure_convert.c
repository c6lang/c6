/* c6 闭包转换诊断：C91/C92/C95 */

// EXPECT test_to_fnptr: error: a closure cannot become a plain function pointer: it carries an environment (call it, or keep it as 'closure RET (parameters)')
// EXPECT test_variadic: error: a variadic function pointer cannot be wrapped into a closure: a trampoline cannot forward an unknown argument list (make it a capturing literal instead)
// EXPECT test_static: error: a closure is not a constant expression: initialize it at run time ('NULL' is fine at file scope)

#if defined test_to_fnptr
int main(void)
{
    int n = 1;
    closure int() f = int() { return n; };
    int (*p)(void) = f;
    return p();
}
#elif defined test_variadic
int vsum(int n, ...) { return n; }
int main(void)
{
    closure int(int, ...) f = vsum;
    f = NULL;
    return 0;
}
#elif defined test_static
int n = 1;
closure int() f = int() { return n; };
int main(void) { return f(); }
#else
int inc(int x) { return x + 1; }
int main(void)
{
    closure int(int) f = inc;
    int r = f(4);
    f = NULL;
    return r != 5;
}
#endif