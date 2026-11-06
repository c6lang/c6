/* c6 extension: an interface used from two translation units.
   Each unit declares the interface itself and converts the same struct,
   so each gets its own (static) vtable object. */
#include <stdio.h>

interface Shape {
    int area();
}

struct Circle { int r; };

/* implemented in 174+_c6_iface_tu.c */
package shapes {
    int area(struct Circle *self);
}
import shapes;

int area_via_vt(struct Circle *c);

static int use(Shape s) { return s.area(); }

int main(void)
{
    struct Circle c = { 4 };
    Shape s = { 0 };

    s = c;
    printf("area=%d again=%d other=%d\n", s.area(), use(c), area_via_vt(&c));
    return 0;
}
