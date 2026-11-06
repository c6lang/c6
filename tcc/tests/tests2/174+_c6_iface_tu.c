/* c6 extension: definitions for 174_c6_iface_tu.c.
   This unit converts the very same struct to the very same interface, so
   it emits its own copy of the vtable. */
interface Shape {
    int area();
}

struct Circle { int r; };

package shapes {
    int area(struct Circle *self) { return self->r * self->r; }
}

int area_via_vt(struct Circle *c)
{
    Shape s = { 0 };
    s = *c;
    return s.area();
}
