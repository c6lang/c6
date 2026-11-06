/* c6 extension: struct methods, 'ret f(struct T *self, ...)' */
#include <stdio.h>

void __tcc_err_clear(void);

struct Vec { int x, y; };
struct Pair { int a, b; };

/* a method is an ordinary function whose first parameter is the receiver */
int vec_sum(struct Vec *v) { return v->x + v->y; }

/* a method can call other methods of the same receiver */
int vec_twice(struct Vec *v) { return v->vec_sum() * 2; }

/* a structure return puts the hidden receiver after the sret buffer */
struct Pair vec_pair(struct Vec *v)
{
    struct Pair p = { v->x * 2, v->y * 3 };
    return p;
}

/* a real field wins over a method of the same name */
struct Holder { int v; int area; };
int holder_area(struct Holder *h) { return h->v * 2; }

/* a prototype is enough: the type may still be incomplete */
struct Node;
int node_depth(struct Node *n);
struct Node { int d; };
int node_depth(struct Node *n) { return n->d; }

/* methods defined inside a package are still found by their bare name */
package shape {
    int scaled(struct Vec *v, int k) { return vec_sum(v) * k; }
    int vec_area(struct Vec *v) { return v->x * v->y; }
}

/* 'T!error' methods work through the receiver syntax as well */
int!error vec_checked(struct Vec *v)
{
    if (v->x < 0)
        err(1, "negative x");
    return v->x;
}

/* 'try' propagates an error raised through the receiver syntax */
int!error checked_times_ten(struct Vec *v)
{
    return (try v->vec_checked()) * 10;
}

static int probe(struct Vec *v)
{
    int x = checked_times_ten(v) catch e {
        printf("caught=%d:%s\n", e.id, e.msg);
        return -1;
    };
    return x;
}

import shape;

int main(void)
{
    struct Vec v = { 3, 4 };
    struct Vec *p = &v;
    struct Pair q = p->vec_pair();
    struct Holder h = { 5, 99 };
    struct Node n = { 7 };

    printf("sum=%d twice=%d\n", p->vec_sum(), p->vec_twice());
    printf("pair=%d,%d\n", q.a, q.b);
    printf("field=%d plain=%d\n", h.area, holder_area(&h));
    printf("depth=%d\n", (&n)->node_depth());
    printf("pkg=%d area=%d\n", p->scaled(10), p->vec_area());

    /* the method is still an ordinary function */
    printf("plain=%d %d\n", vec_sum(p), shape.vec_area(p));

    /* 'impl' is a plain identifier now */
    {
        int impl = 1;
        printf("impl=%d\n", impl);
    }

    /* a fallible method, handled with 'catch' */
    printf("probe=%d\n", probe(p));
    p->x = -1;
    printf("probe=%d\n", probe(p));
    p->x = 3;
    __tcc_err_clear();
    return 0;
}
