/* c6 编译期诊断：变量定义必须初始化（VLA 与 static 变量除外）
 *
 * 每个片段单独用 -D<片段名> 编译（tests/run-errors.sh 驱动），
 * `// EXPECT <片段名>: <诊断文本>` 行声明该片段必须产生的诊断。
 * 不带任何 -D 时是本文件的“正常路径”，必须编译通过。
 */
struct S { int a; };

// EXPECT test_local_int: error: variable 'x' must be initialized
// EXPECT test_local_multi: error: variable 'y' must be initialized
// EXPECT test_local_array: error: variable 'a' must be initialized
// EXPECT test_local_struct: error: variable 's' must be initialized
// EXPECT test_local_pointer: error: variable 'p' must be initialized
// EXPECT test_global_int: error: variable 'g' must be initialized
// EXPECT test_for_init: error: variable 'k' must be initialized

#if defined test_local_int
static int f(void) { int x; return x; }
int main(void) { return f(); }
#elif defined test_local_multi
static int f(void) { int x = 0, y; return x + y; }
int main(void) { return f(); }
#elif defined test_local_array
static int f(void) { int a[3]; return a[0]; }
int main(void) { return f(); }
#elif defined test_local_struct
static int f(void) { struct S s; return s.a; }
int main(void) { return f(); }
#elif defined test_local_pointer
static int f(void) { int *p; return p == 0; }
int main(void) { return f(); }
#elif defined test_global_int
int g;
int main(void) { return g; }
#elif defined test_for_init
static int f(void) { int n = 0; for (int k; k < 1; k++) n++; return n; }
int main(void) { return f(); }
#elif defined test_ok
extern int e;
typedef int myint;
struct S2 { int a; };
int g = 0;
static int sg = 0;
int array[3] = {1, 2, 3};
struct S s = {0};
int main(void)
{
    int x = 0;
    int y = 1, z = 2;
    int a[2] = {0};
    int *p = 0;
    struct S ls = {0};
    static int sl = 0;
    myint m = 3;
    extern int le;
    for (int k = 0; k < 1; k++) x += k;
    if (x + y + z + a[0] + (p == 0) + ls.a + sl + m + g + sg + array[0]
        + s.a != 8)
        return 1;
    return 0;
}
#elif defined test_vla_ok
int main(void)
{
    int n = 3;
    int a[n];
    a[0] = 1;
    return a[0] + n - 4;
}
#else
/* 正常路径：static 变量不需要初始化器（C 保证静态存储期对象清零），
   文件作用域和块作用域都必须编译通过，而且拿到的确实是 0。 */
static int sg;
static int sl_fn(void) { static int sl; return ++sl; }
int main(void)
{
    sg += 2;
    if (sg != 2)
        return 1;
    if (sl_fn() != 1 || sl_fn() != 2)   /* static 局部记得上一次的值 */
        return 1;
    return 0;
}
#endif
