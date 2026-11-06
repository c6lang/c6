/* c6 extension: slices 'T[:]' */
#include <stdio.h>

struct Pair { int a, b; };
typedef int Row[3];                 /* an array element type needs a name */

/* a slice is passed by value and may be returned */
int sum(int[:] s)
{
    int i = 0, total = 0;
    for (i = 0; i < (int)s.len; i++)
        total += s[i];
    return total;
}

int[:] tail(int[:] s)
{
    return s[1:];
}

/* a slice can be a struct member */
struct Holder { int[:] s; };

int holder_sum(struct Holder *h)
{
    return sum(h->s);
}

package view {
    int first(int[:] s) { return s.len > 0 ? s[0] : -1; }
}
import view;

int main(void)
{
    int a[5] = { 10, 20, 30, 40, 50 };
    int[:] s = a;
    int i = 0;

    /* array to slice, .len and .data, plain indexing */
    printf("len=%d s0=%d s4=%d\n", (int)s.len, s[0], s[4]);
    printf("data=%d\n", s.data == a);

    /* writes go through to the array and vice versa */
    s[0] = 99;
    a[1] = 21;
    printf("a0=%d s1=%d\n", a[0], s[1]);

    /* ranges: [lo:hi], [lo:], [:hi], [:] */
    {
        int[:] t = a[1:4];
        int[:] u = t[1:];
        int[:] v = s[:2];
        int[:] w = t[:];
        printf("t=%d,%d,%d u=%d,%d v=%d w=%d\n",
               (int)t.len, t[0], t[2], (int)u.len, u[0], (int)v.len, (int)w.len);
    }

    /* an empty range is legal */
    printf("empty=%d\n", (int)s[2:2].len);

    /* slices through functions */
    printf("sum=%d tail=%d,%d\n", sum(s), (int)tail(s).len, tail(s)[0]);

    /* slices in structs and packages */
    {
        struct Holder h = { s[2:4] };
        printf("holder=%d pkg=%d\n", holder_sum(&h), view.first(h.s));
    }

    /* an array of slices */
    {
        int[:] parts[2] = { s[0:2], s[2:4] };
        printf("parts=%d,%d,%d\n", (int)parts[0].len, parts[0][1], parts[1][0]);
    }

    /* a pointer to a slice */
    {
        int[:] *p = &s;
        printf("ptr=%d,%d\n", (int)p->len, (*p)[3]);
    }

    /* element types: struct, char (string literal), array via typedef */
    {
        struct Pair pairs[3] = { {1, 2}, {3, 4}, {5, 6} };
        struct Pair[:] ps = pairs;
        ps[1].a = 30;
        printf("pairs=%d,%d,%d\n", ps[1].a, ps[2].b, (int)ps.len);
    }
    {
        char[:] m = "hello";
        printf("str=%d,%c,%c\n", (int)m.len, m[0], m[4]);
    }
    {
        int grid[2][3] = { {1, 2, 3}, {4, 5, 6} };
        Row[:] rows = grid;
        printf("rows=%d,%d,%d\n", (int)rows.len, rows[0][1], rows[1][2]);
    }

    /* a compound literal builds a slice from a pointer and a length */
    {
        int[:] lit = (int[:]){ a + 2, 2 };
        printf("lit=%d,%d,%d\n", (int)lit.len, lit[0], lit[1]);
    }

    /* sizeof: the slice is two machine words, an element is a plain object */
    printf("sizes=%d,%d,%d\n", (int)sizeof(int[:]),
           (int)sizeof(s[0]), (int)sizeof(s.len));

    /* loops, side effects in the index, re-slicing */
    for (i = 0; i < (int)s.len; i++)
        s[i] += 1;
    printf("bumped=%d,%d\n", a[0], a[4]);
    {
        int j = 1;
        printf("post=%d j=%d\n", s[j++], j);
    }
    return 0;
}
