/* c6 extension: interface / impl (fat pointer + vtable) */
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

impl Shape for Circle {
    int area(Circle *self) { return 3 * self->r * self->r; }
    const char *name(Circle *self) { return "circle"; }
}

impl Shape for Rect {
    int area(Rect *self) { return self->w * self->h; }
    const char *name(Rect *self) { return "rect"; }
}

impl Printable for Circle {
    void print(Circle *self) { printf("Circle(%d)", self->r); }
}

impl Printable for Rect {
    void print(Rect *self) { printf("Rect(%dx%d)", self->w, self->h); }
}

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
    return 0;
}
