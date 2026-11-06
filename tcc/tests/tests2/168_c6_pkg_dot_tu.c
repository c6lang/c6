/* c6 extension: 'pkg.member' across translation units.
   This file plays the part of a header: it declares the package API with
   'pkg.member' and lets the other unit provide the symbols. */
#include <stdio.h>

package math {
    int add(int a, int b);
    int bump(void);
    extern int counter;
}

import math;

int main(void)
{
    printf("add=%d\n", math.add(2, 3));
    math.counter = 5;
    printf("counter=%d\n", math.counter);
    printf("bump=%d\n", math.bump());
    return 0;
}
