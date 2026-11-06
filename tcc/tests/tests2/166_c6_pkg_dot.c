/* c6 extension: 'pkg.member' explicit package access */
#include <stdio.h>

package math {
    int counter = 0;
    int add(int a, int b) { return a + b; }
    /* inside 'package math' the qualified spelling needs no import */
    int twice(int a) { return math.add(a, a); }
    struct Point { int x, y; };
    struct Point origin = {3, 4};
    int bump(void) { return ++counter; }
}

/* 'import' is allowed inside a package block and names the rest of it */
package text {
    import math;
    int len(const char *s)
    {
        int n = 0;
        while (s[n])
            n++;
        return n;
    }
    int len_plus(int k) { return len("ab") + math.add(k, 1); }
}

import math;
import text;

/* a qualified package function is an ordinary symbol: its address works */
int (*add_ref)(int, int) = math.add;

int main(void)
{
    /* qualified access, mixed with unqualified access from 'import' */
    printf("add=%d %d\n", math.add(2, 3), add(4, 5));
    printf("twice=%d len=%d\n", math.twice(21), text.len("hello"));
    printf("len_plus=%d\n", text.len_plus(1));

    /* package globals: read, address-of and assignment */
    printf("counter=%d\n", math.counter);
    printf("bump=%d\n", math.bump());
    printf("bump=%d\n", math.bump());
    math.counter = 10;
    printf("counter=%d\n", math.counter);
    printf("origin=%d,%d\n", math.origin.x, math.origin.y);
    *&math.origin.x = 8;
    printf("origin=%d\n", math.origin.x);
    printf("add_ref=%d\n", add_ref(20, 22));

    /* the mangled spelling keeps working and names the same object */
    math__counter = 42;
    printf("mangled=%d %d\n", math__add(10, 20), math__counter);

    /* a visible object wins over the package name: plain C member access */
    {
        struct Point math = {7, 8};
        printf("shadow=%d\n", math.x);
    }

    return 0;
}
