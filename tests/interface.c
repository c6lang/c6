/* superc 自测：结构化接口（胖指针 + vtable）
 *
 * 结构体只要声明了接口要求的全部方法就实现了该接口，没有 impl 声明；
 * 检查是惰性的，在第一次转换（赋值、实参、强转）时进行。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=tcc-ext -test tests/interface.c tests/tu/interface.c
 */
#include <stdio.h>
#include <string.h>

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

/* 不同接收者可以有同名方法：包把它们分开 */
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

/* 结构体值隐式转成接口值 */
static int describe(Shape s)
{
    return s.area();
}

static const char *name_of(Shape s)
{
    return s.name();
}

/* 接口值就是个普通的结构体局部变量，可以按值拷贝 */
static int twice_area(Shape s)
{
    Shape t = s;
    return t.area();
}

/* static inline 函数体在单元末尾才编译，vtable 仍然要生成 */
static inline int inline_area(Circle c)
{
    Shape s = {0};
    s = c;
    return s.area();
}

/* ---- 跨翻译单元（tests/tu/interface.c）----
 * 两个 TU 各自声明同一个接口、转换同一个结构体，各自生成一份 static vtable。 */
interface Size {
    int area();
}

struct Sq { int s; };

package sq {
    int area(struct Sq *self);      /* 定义在 tests/tu/interface.c */
}

int area_via_vt(struct Sq *c);

/* ---- 测试块 ---- */

test "结构体隐式转为接口并按 vtable 分派" {
    Circle c = {0};
    Rect r = {0};
    c.r = 5;
    r.w = 4;
    r.h = 6;
    if (describe(c) != 75)
        err(1, "circle 的 area 不对");
    if (describe(r) != 24)
        err(2, "rect 的 area 不对");
    if (strcmp(name_of(c), "circle"))
        err(3, "circle 的 name 不对");
    if (strcmp(name_of(r), "rect"))
        err(4, "rect 的 name 不对");
}

test "接口值可以按值拷贝，仍然指向同一个对象" {
    Circle c = {0};
    c.r = 5;
    if (twice_area(c) != 75)
        err(1, "拷贝出来的接口值分派不对");
}

test "显式强转语法也可以" {
    Circle c = {0};
    Shape s = {0};
    c.r = 5;
    s = (Shape)c;
    if (s.area() != 75)
        err(1, "(Shape)c 之后分派不对");
}

test "接口值是胖指针：两个机器字" {
    if ((int)sizeof(Shape) != 2 * (int)sizeof(void *))
        err(1, "sizeof(Shape) 不是两个机器字");
}

test "static inline 函数体里的转换" {
    Circle c = {0};
    c.r = 5;
    if (inline_area(c) != 75)
        err(1, "inline 函数里的转换不对");
}

test "同一个接口可以由多个结构体实现" {
    Circle c = {0};
    Rect r = {0};
    Shape s = {0};
    c.r = 5;
    r.w = 4;
    r.h = 6;
    s = c;
    if (s.area() != 75)
        err(1, "先转成 circle 后不对");
    s = r;
    if (s.area() != 24)
        err(2, "同一个接口变量改指向 rect 后不对");
}

test "接口跨翻译单元：另一个 TU 转换同一个结构体" {
    struct Sq q = { 4 };
    Size z = { 0 };
    z = q;
    if (z.area() != 16)
        err(1, "本 TU 的 vtable 分派不对");
    if (area_via_vt(&q) != 16)
        err(2, "另一个 TU 的 vtable 分派不对");
}
