/* c6 extension: a method declared in this unit, defined in another */
#include <stdio.h>

struct Vec { int x, y; };

/* the prototype alone makes the call syntax available */
int vec_sum(struct Vec *v);
int vec_scale(struct Vec *v, int k);

int main(void)
{
    struct Vec v = { 3, 4 };
    struct Vec *p = &v;

    printf("sum=%d scaled=%d\n", p->vec_sum(), p->vec_scale(10));

    /* still an ordinary function */
    printf("plain=%d\n", vec_sum(p));
    return 0;
}
