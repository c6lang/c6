/* c6 自测：结构体方法（'ret f(struct T *self, ...)'，调用写作 p->f()）
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/method.c tests/tu/method.c
 */
#include <stdio.h>
#include <string.h>

void __tcc_err_clear(void);

struct Vec { int x, y; };
struct Pair { int a, b; };

/* 方法就是第一个参数是接收者的普通函数 */
int vec_sum(struct Vec *v) { return v->x + v->y; }

/* 方法可以调用同一个接收者的其它方法 */
int vec_twice(struct Vec *v) { return v->vec_sum() * 2; }

/* 返回结构体时，隐藏的接收者在 sret 缓冲区之后 */
struct Pair vec_pair(struct Vec *v)
{
    struct Pair p = { v->x * 2, v->y * 3 };
    return p;
}

/* 真字段优先于同名方法 */
struct Holder { int v; int area; };
int holder_area(struct Holder *h) { return h->v * 2; }

/* 只要原型就够：类型可以还是不完整的 */
struct Node;
int node_depth(struct Node *n);
struct Node { int d; };
int node_depth(struct Node *n) { return n->d; }

/* 包里定义的方法用裸名字照样找得到 */
package shape {
    int scaled(struct Vec *v, int k) { return vec_sum(v) * k; }
    int vec_area(struct Vec *v) { return v->x * v->y; }
}
import shape;

/* 'T!error' 方法同样可以走接收者语法 */
int!error vec_checked(struct Vec *v)
{
    if (v->x < 0)
        err(1, "negative x");
    return v->x;
}

/* 'try' 把通过接收者语法抛出的错误原样传回 */
int!error checked_times_ten(struct Vec *v)
{
    return (try v->vec_checked()) * 10;
}

static int probe(struct Vec *v)
{
    int x = checked_times_ten(v) catch e {
        return -e.id;
    };
    return x;
}

/* ---- 跨翻译单元（tests/tu/method.c）---- */
int vec_scale(struct Vec *v, int k);
int tu_scale(struct Vec *v, int k);

/* ---- 测试块 ---- */

test "方法调用就是普通函数调用" {
    struct Vec v = { 3, 4 };
    struct Vec *p = &v;
    if (p->vec_sum() != 7)
        err(1, "p->vec_sum() 不对");
    if (vec_sum(p) != 7)
        err(2, "普通调用形式不对");
    if (p->vec_twice() != 14)
        err(3, "方法调用另一个方法不对");
}

test "返回结构体的方法" {
    struct Vec v = { 3, 4 };
    struct Vec *p = &v;
    struct Pair q = p->vec_pair();
    if (q.a != 6 || q.b != 12)
        err(1, "sret 方法返回的结构体不对");
}

test "真字段优先于同名方法" {
    struct Holder h = { 5, 99 };
    if (h.area != 99)
        err(1, "字段没有优先");
    if (holder_area(&h) != 10)
        err(2, "同名方法本身仍可普通调用");
}

test "只有原型、类型尚不完整时也能调用方法" {
    struct Node n = { 7 };
    if ((&n)->node_depth() != 7)
        err(1, "不完整类型的方法调用不对");
}

test "包里定义的方法按裸名字查找" {
    struct Vec v = { 3, 4 };
    struct Vec *p = &v;
    if (p->scaled(10) != 70)
        err(1, "包方法 p->scaled 不对");
    if (p->vec_area() != 12)
        err(2, "包方法 p->vec_area 不对");
    if (shape.vec_area(p) != 12)
        err(3, "限定调用 shape.vec_area 不对");
}

test "impl 现在是普通标识符" {
    int impl = 1;
    if (impl != 1)
        err(1, "impl 不能当普通变量名");
}

test "T!error 方法通过接收者语法传播错误" {
    struct Vec v = { 3, 4 };
    struct Vec *p = &v;
    __tcc_err_clear();
    if (probe(p) != 30)
        err(1, "正常路径的返回值不对");
    p->x = -1;
    if (probe(p) != -1)
        err(2, "错误路径没有把错误码带回来");
    __tcc_err_clear();
}

test "方法可以跨翻译单元：本 TU 声明、另一个 TU 定义" {
    struct Vec v = { 3, 4 };
    struct Vec *p = &v;
    if (p->vec_scale(10) != 70)
        err(1, "另一个 TU 的 vec_scale 不对");
    if (p->tu_scale(2) != 14)
        err(2, "另一个 TU 的方法里调用本 TU 的方法不对");
    if (vec_scale(p, 1) != 7)
        err(3, "跨 TU 的普通调用形式不对");
}
