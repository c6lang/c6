/* c6 extension: definitions for 171_c6_method_tu.c */
struct Vec { int x, y; };

int vec_sum(struct Vec *v) { return v->x + v->y; }
int vec_scale(struct Vec *v, int k) { return v->vec_sum() * k; }
