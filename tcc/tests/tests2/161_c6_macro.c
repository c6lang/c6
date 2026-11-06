/* c6 extension: '#macro' with hygienic '__local' names */
#include <stdio.h>

#macro swap(a, b) {
    typeof(a) __local tmp = (a);
    (a) = (b);
    (b) = tmp;
}

#macro maxv(x, y, out) {
    typeof(x) __local m = (x);
    if ((y) > m)
        m = (y);
    (out) = m;
}

#macro twice(stmt) {
    stmt;
    stmt;
}

#macro outer(a, b) {
    typeof(a) __local t = (a);
    swap(t, b);
    (a) = t;
}

int main(void)
{
    int a = 3, b = 9, m = 0;

    swap(a, b);
    printf("swap: %d %d\n", a, b);

    maxv(a, b, m);
    printf("maxv: %d\n", m);

    outer(a, b);
    printf("outer: %d %d\n", a, b);

    twice(printf("twice\n"));

    /* a user variable with the same name must not be captured */
    {
        int tmp = 42;
        swap(tmp, a);
        printf("user: tmp=%d a=%d\n", tmp, a);
    }

    /* two expansions in one scope must not clash */
    swap(a, b);
    swap(a, b);
    printf("twice-swap: %d %d\n", a, b);

    /* no leftover identifiers */
    printf("names: %d\n", (int)sizeof(a));
    return 0;
}
