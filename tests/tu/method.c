/* tests/method.c 的第二个翻译单元：方法的实现。
 *
 * 只要原型在调用点可见，'p->f()' 就能用；定义可以放在任何 TU。
 */
struct Vec { int x, y; };

/* 由 tests/method.c 定义 */
int vec_sum(struct Vec *v);

int vec_scale(struct Vec *v, int k) { return v->vec_sum() * k; }
int tu_scale(struct Vec *v, int k) { return v->vec_scale(k); }

/* 另一个 depth：和 tests/method.c 里的 depth(struct Vec *) 同名，
   符号分别是 Depth_depth / Vec_depth */
struct Depth { int d; };
int depth(struct Depth *p) { return p->d * 4; }
