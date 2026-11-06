/* c6 自测：tag 名直接当类型名（省略 struct / union / enum）
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/typename.c tests/tu/typename.c
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>       /* 'struct stat' 与函数 'stat()' 同名：验证遮蔽规则 */

struct Vec { int x, y; };

/* 定义体内就能省：成员是指向自己的指针 */
struct Node { int v; Node *next; };

union Word { int i; float f; };

enum Color { RED = 1, GREEN = 2, BLUE = 3 };

/* 成员用裸 tag 声明 */
struct Outer { Vec v; Color c; Word w; };

typedef Vec VecAlias;               /* typedef 的目标也可以是裸 tag */

/* 文件作用域的静态对象 */
static Vec g_vec = { 7, 8 };

/* 形参和返回值都用裸 tag */
static Vec vec_add(Vec a, Vec b)
{
    Vec r = { a.x + b.x, a.y + b.y };
    return r;
}

/* 第一个形参是指向 struct Vec 的指针 => 注册为 Vec 的方法 */
static int vec_len(Vec *self) { return self->x + self->y; }

/* 切片元素是文件作用域的 tag */
static int sum_vec(Vec[:] s)
{
    int i = 0, total = 0;
    for (i = 0; i < (int)s.len; i++)
        total += s[i].x + s[i].y;
    return total;
}

/* tag 与普通符号同名时普通命名空间优先：这两行在 C 里本来就合法 */
struct file { int fd; };
static int file(void) { return 5; }

/* 标签与 tag 同名：语句开头的 'L:' 仍然是标签 */
struct L { int v; };

/* ---- 跨翻译单元（tests/tu/typename.c）---- */
Vec tu_scale(Vec v, int k);

/* ---- 测试块 ---- */

test "声明、成员访问与 sizeof" {
    Vec v = { 3, 4 };
    if (v.x != 3 || v.y != 4)
        err(1, "裸 tag 声明的变量不对");
    if (sizeof(Vec) != sizeof(struct Vec))
        err(2, "裸 tag 与 struct 形式的 sizeof 不一致");
    if (sizeof(Vec *) != sizeof(void *))
        err(3, "裸 tag 指针的大小不对");
    if (g_vec.x != 7 || g_vec.y != 8)
        err(4, "文件作用域的裸 tag 对象不对");
}

test "指针、数组与 '->'：语句开头的 'Vec *p' 是声明" {
    Vec v = { 3, 4 };
    Vec *p = &v;
    if (p->x != 3 || (*p).y != 4)
        err(1, "裸 tag 指针不对");

    Vec arr[2] = { { 1, 2 }, { 3, 4 } };
    if (arr[0].x + arr[1].y != 5)
        err(2, "裸 tag 数组不对");

    Vec *q = arr;
    if ((q + 1)->y != 4)
        err(3, "指针算术不对");
}

test "形参与返回值" {
    Vec a = { 1, 2 };
    Vec b = { 10, 20 };
    Vec c = vec_add(a, b);
    if (c.x != 11 || c.y != 22)
        err(1, "裸 tag 的形参/返回值不对");
}

test "结构体成员与自引用" {
    struct Outer o = { { 1, 2 }, GREEN, { 3 } };
    if (o.v.y != 2 || o.c != GREEN || o.w.i != 3)
        err(1, "裸 tag 成员不对");

    Node c = { 3, 0 };
    Node b = { 2, &c };
    Node a = { 1, &b };
    int sum = 0;
    Node *it = &a;
    while (it) {
        sum += it->v;
        it = it->next;
    }
    if (sum != 6)
        err(2, "自引用链表不对");
}

test "union 与 enum 的裸名" {
    Word w = { 0 };
    w.i = 42;
    if (w.i != 42)
        err(1, "union 裸名不对");
    if (sizeof(Word) != sizeof(union Word))
        err(2, "union 裸名的 sizeof 不一致");

    Color c = BLUE;
    if (c != 3)
        err(3, "enum 裸名不对");
    if ((Color)GREEN != GREEN)
        err(4, "enum 裸名强转不对");
    switch (c) {
    case RED:  err(5, "switch 走错分支");
    case BLUE: break;
    default:   err(6, "switch 没命中");
    }
}

test "sizeof、强转与复合字面量" {
    Vec *p = (Vec *)0;
    if (p != 0)
        err(1, "裸 tag 强转不对");

    Vec v = (Vec){ 5, 6 };
    if (v.x != 5 || v.y != 6)
        err(2, "裸 tag 复合字面量不对");

    if ((int)sizeof(Vec) != (int)sizeof(struct Vec))
        err(3, "sizeof 不一致");
    if (sizeof(struct Outer) < sizeof(Vec))
        err(4, "成员布局不对");
}

test "切片元素用裸 tag" {
    Vec arr[3] = { { 1, 1 }, { 2, 2 }, { 3, 3 } };
    Vec[:] s = arr;
    if ((int)s.len != 3)
        err(1, "切片长度不对");
    if (s[2].x != 3)
        err(2, "切片下标不对");
    if (sum_vec(s) != 12)
        err(3, "切片形参不对");
}

test "方法是普通函数：裸 tag 写接收者" {
    Vec v = { 3, 4 };
    if ((&v)->vec_len() != 7)
        err(1, "裸 tag 接收者的方法调用不对");
    if (vec_len(&v) != 7)
        err(2, "普通调用形式不对");
}

test "限定符与 typedef" {
    Vec v = { 1, 2 };
    const Vec *cp = &v;
    if (cp->x != 1)
        err(1, "const 裸 tag 指针不对");
    if (sizeof(const Vec) != sizeof(Vec))
        err(2, "const 裸 tag 的大小不对");

    VecAlias a = { 9, 10 };
    if (a.x + a.y != 19)
        err(3, "typedef 裸 tag 不对");
    if (sizeof(VecAlias) != sizeof(struct Vec))
        err(4, "typedef 裸 tag 的 sizeof 不对");
}

test "遮蔽：同名函数优先，struct 形式照旧" {
    struct file f = { 3 };           /* 被函数遮蔽时必须写 struct */
    if (f.fd != 3)
        err(1, "struct file 不对");
    if (file() != 5)
        err(2, "同名函数被 tag 抢走了");
}

test "遮蔽：同名局部变量优先（按表达式解析）" {
    int Vec = 3;
    Vec = 4;
    if (Vec != 4)
        err(1, "局部变量赋值不对");
    if (Vec * 2 != 8)
        err(2, "局部变量没有优先于 tag");
}

test "真实头文件：struct stat 与 stat() 共存" {
    struct stat st = { 0 };
    if (stat(".", &st) != 0)
        err(1, "stat() 调用被 tag 抢走了");
}

test "标签 'TAG:' 仍然是标签" {
    int n = 0;
    goto L;
    n = 1;
L:
    n += 2;
    if (n != 2)
        err(1, "标签被当成类型解析了");
}

test "跨翻译单元：两个 TU 都写裸 tag" {
    Vec v = { 2, 3 };
    Vec w = tu_scale(v, 4);
    if (w.x != 8 || w.y != 12)
        err(1, "跨 TU 的裸 tag 类型不兼容");
}
