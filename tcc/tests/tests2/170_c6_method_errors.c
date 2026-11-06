/* c6 extension: diagnostics for struct methods */
struct Vec { int x, y; };

int vec_sum(struct Vec *v) { return v->x + v->y; }

#if defined test_method_without_call
int main(void)
{
    struct Vec v = { 1, 2 };
    struct Vec *p = &v;
    return p->vec_sum;
}
#elif defined test_duplicate_method
package a { int area(struct Vec *v) { return v->x; } }
package b { int area(struct Vec *v) { return v->y; } }
int main(void) { return 0; }
#elif defined test_field_takes_precedence
struct Holder { int v; int vec_sum; };
int main(void)
{
    struct Holder h = { 1, 2 };
    struct Holder *p = &h;
    return p->vec_sum == 2 ? 0 : 1;     /* a field, not a method */
}
#elif defined test_value_receiver_needs_arrow
int main(void)
{
    struct Vec v = { 1, 2 };
    return v.vec_sum();         /* only '->' is a method call */
}
#elif defined test_unknown_member
int main(void)
{
    struct Vec v = { 1, 2 };
    struct Vec *p = &v;
    return p->nope;
}
#else
int main(void)
{
    struct Vec v = { 1, 2 };
    struct Vec *p = &v;
    return p->vec_sum() == 3 ? 0 : 1;
}
#endif
