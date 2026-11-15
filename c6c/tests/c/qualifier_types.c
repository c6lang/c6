/* 指针级限定符进 type id（`int *const` / `int *restrict` / 数组上的 const）：
 * 以前只有"被声明的对象"那一层记得住 const，指针自己那层的限定符一吃掉就没了，
 * 于是 `__builtin_types_compatible_p(int **, restricted_pointer *)` 给出 1
 * （gcc 给 0）。tcc 上游 100_c99array-decls 就是拿这个当用例。
 *
 * 这个测试只放 gcc 与 c6c 结果一致的断言（gcc 当 oracle，逐条比输出）。 */
#include <stdio.h>

typedef int *restrict restricted_pointer;
typedef int *pointer_array[2];
typedef int *restrict restricted_pointer_array;

/* 指针数组上的 restrict 归元素（C11 6.7.3p9）⇒ 两个 typedef 是同一类型 */
pointer_array plain_pa;
restricted_pointer_array restricted_pa;

/* 形参里的 `[const N]` 归调整后的指针（C11 6.7.6.3p7） */
static int takes_const_ptr(int x[const 4])
{
    return x[2];
}

static int takes_restrict_ptr(int *restrict p)
{
    return *p;
}

/* `T *const` 作为形参类型：顶层限定符在函数类型里被丢掉，但它是同一个类型 */
static int takes_ptr_const(int *const p)
{
    return *p + 1;
}

int main(void)
{
    int v = 41;
    int arr[4] = {10, 20, 30, 40};
    int *const cp = &v;              /* 指针自己 const */
    const int *pc = &v;              /* 指向的对象 const */
    int *restrict rp = &v;
    int **pp = 0;

    /* 指针层的 const/restrict 不该和裸指针混为一谈 */
    printf("compat ptr/ptr-const: %d\n", __builtin_types_compatible_p(int *, int *const));
    printf("compat ptr/ptr-restrict: %d\n",
           __builtin_types_compatible_p(int *, int *restrict));
    printf("compat int**/restrict-ptr*: %d\n",
           __builtin_types_compatible_p(int **, restricted_pointer *));
    printf("compat pa/restricted-pa: %d\n",
           __builtin_types_compatible_p(pointer_array, restricted_pointer_array));
    printf("compat typeof(cp,rp) vs int*: %d %d\n",
           __builtin_types_compatible_p(__typeof__(cp), int *),
           __builtin_types_compatible_p(__typeof__(rp), int *));

    printf("calls: %d %d %d\n", takes_const_ptr(arr), takes_restrict_ptr(&v),
           takes_ptr_const(&v));
    printf("values: %d %d %d\n", *cp, *pc, *rp);
    (void)pp;
    (void)plain_pa;
    (void)restricted_pa;
    return 0;
}
