/* c6 extension: a slice API shared by two translation units.
   Each unit declares 'int[:]' itself; the layout is the same on both
   sides, so no extra declaration is needed. */
#include <stdio.h>

/* defined in 177+_c6_slice_tu.c */
int sum(int[:] s);
int[:] tail(int[:] s);
int first(int[:] s);

int main(void)
{
    int a[5] = { 1, 2, 3, 4, 5 };
    int[:] s = a;
    int[:] t = tail(s);

    printf("sum=%d first=%d\n", sum(s), first(s));
    printf("tail=%d,%d,%d\n", (int)t.len, t[0], t[t.len - 1]);

    /* a slice made in this unit, consumed by the other one */
    printf("sum2=%d\n", sum(a[1:3]));
    return 0;
}
