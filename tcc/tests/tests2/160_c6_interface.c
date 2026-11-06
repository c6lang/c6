/* c6 extension: interfaces (fat pointer + vtable).
   A struct implements an interface by having the methods: there is no
   'impl' declaration any more, the vtable is generated on demand. */
#include <stdio.h>

interface Shape {
    int area();
    const char *name();
}

interface Printable {
    void print();
}

struct Circle { int r; };
struct Rect { int w, h; };
typedef struct Circle Circle;
typedef struct Rect Rect;

/* same method names for different types: the package keeps them apart */
package circle {
    int area(Circle *self) { return 3 * self->r * self->r; }
    const char *name(Circle *self) { return "circle"; }
    void print(Circle *self) { printf("Circle(%d)", self->r); }
}

package rect {
    int area(Rect *self) { return self->w * self->h; }
    const char *name(Rect *self) { return "rect"; }
    void print(Rect *self) { printf("Rect(%dx%d)", self->w, self->h); }
}

import circle;
import rect;

/* implicit conversion of a struct value to the interface value */
static int describe(Shape s)
{
    printf("%s:%d ", s.name(), s.area());
    return s.area();
}

/* an interface value as an ordinary struct local */
static void show(Shape s)
{
    printf("[%s]", s.name());
}

static void dump(Printable p)
{
    p.print();
    printf(" ");
}

/* a conversion inside a 'static inline' body: the body is compiled at the
   very end of the unit, so its vtable must still be generated */
static inline int inline_area(Circle c)
{
    Shape s = {0};
    s = c;
    return s.area();
}

int main(void)
{
    Circle c = {0};
    Rect r = {0};
    Shape s = {0};

    c.r = 5;
    r.w = 4;
    r.h = 6;

    printf("total=%d\n", describe(c) + describe(r));

    s = c;
    show(s);
    s = r;
    show(s);
    printf("\n");

    dump(c);
    dump(r);
    printf("\n");

    /* a copy of an interface value still points at the same object */
    s = c;
    show(s);
    show(s);
    printf("\n");

    /* explicit cast syntax also works */
    show((Shape)c);
    printf("\n");

    printf("sizeof=%d\n", (int)sizeof(Shape));
    printf("inline=%d\n", inline_area(c));
    return 0;
}
