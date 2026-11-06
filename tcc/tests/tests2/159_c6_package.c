/* c6 extension: package / import */
#include <stdio.h>

package math {
    int add(int a, int b) { return a + b; }
    int twice(int a) { return add(a, a); }
    int hidden_helper(int a) { return a + 1; }
}

package text {
    int len(const char *s)
    {
        int n = 0;
        while (s[n])
            n++;
        return n;
    }
}

import math;
import text;

int main(void)
{
    /* imported names resolve to the mangled symbols */
    printf("add=%d\n", add(2, 3));
    printf("twice=%d\n", twice(21));
    printf("len=%d\n", len("hello"));

    /* the mangled names are ordinary global symbols */
    printf("mangled=%d %d\n", math__add(10, 20), text__len("abc"));

    /* a package can see its own members without qualification */
    printf("helper=%d\n", math__hidden_helper(41));
    return 0;
}
