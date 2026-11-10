/* c6 捕获式字面量不能退化成普通函数指针（C93） */

// EXPECT test_arg: this literal captures 'n', so its value is a closure, not a function pointer: use 'closure RET (parameters)' as the type, or pass the value in as a parameter
// EXPECT test_return: this literal captures 'n', so its value is a closure, not a function pointer: use 'closure RET (parameters)' as the type, or pass the value in as a parameter

#if defined test_arg
static int apply(int (*f)(int), int v) { return f(v); }
int main(void) { int n = 1; return apply(int(int x) { return x + n; }, 2); }
#elif defined test_return
typedef int (*FP)(int);
static FP make(int n) { return int(int x) { return x + n; }; }
int main(void) { return 0; }
#else
int main(void) { return 0; }
#endif
