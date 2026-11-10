/* c6 自测：函数模板与 typedef 模板（`|T|` 前缀）
 *
 * 结构体模板的名字在花括号之后，形参写在名字后面（`struct Vect@T { … }`）；
 * 函数的返回类型在名字**左边**，typedef 的目标类型也在名字左边，所以这两种
 * 声明把形参表写在整个声明的最前面（A3「参数必须先于使用」）：
 *
 *   |T| T vat(Vect@T *self, int i) { … }
 *   |T| typedef Vect@T MyVec;
 *
 * 模板本身既不是函数也不是类型：`vat(0)` 报 C103（要写 `vat@int`，或者让
 * 接收者说出 T：`p->vat(0)` 里 `p` 已经是 `Vect@int`）。实例化会把签名当
 * 普通声明重放一遍（形参绑成普通 typedef），函数体排队到编译单元末尾再当
 * 普通函数编译——所以函数指针、`go`/`thread`、`-emit-header` 看到的都是
 * 普通函数 `Vect_int_vat`。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/generic_fn.c
 * 或：
 *   make selftest
 */
#include <stdio.h>
#include <string.h>

/* ---- 被测的东西 ---- */

struct Vect@T { T *data; int len; };
struct Map@(K, V) { K key; V val; };

/* 一份方法覆盖所有实例：接收者写成 Vect@T */
|T| T vat(Vect@T *self, int i) { return self->data[i]; }
|T| void push(Vect@T *self, T x) { self->data[self->len++] = x; }
|T| int vsum(Vect@T *self)
{
    int i = 0, s = 0;
    for (i = 0; i < self->len; i++)
        s += (int)vat@T(self, i);
    return s;
}
|T| T last(Vect@T *self) { return vat@T(self, self->len - 1); }

/* 普通函数模板 */
|T| T id(T x) { return x; }
|T| T max2(T a, T b) { return a > b ? a : b; }
|T| int nel(T[:] s) { return s.len; }

/* 先声明、后定义：和普通函数一样 */
|K, V| V second(Map@(K, V) *m);
|K, V| V second(Map@(K, V) *m) { return m->val; }

/* 声明时接收者的模板还没写出来也允许：实例化时再解析 */
|T| T only(Later@T *self);
struct Later@T { T v; };
|T| T only(Later@T *self) { return self->v; }

/* typedef 模板：结果就是普通 typedef，能当任何类型用 */
|T| typedef Vect@T MyVec;
|T| typedef struct { T v; int n; } Box;
|T| typedef T *Ptr;

/* 可失败的模板：返回类型照写 'T!error' */
|T| T!error vget(Vect@T *self, int i)
{
    if (i < 0 || i >= self->len)
        err(5, "out of range");
    return self->data[i];
}

static int get_or(Vect@int *v, int i)
{
    int r = vget@int(v, i) catch e {
        return -1;
    };
    return r;
}

test "一份方法覆盖所有实例" {
    int d[3] = {1, 2, 3};
    long ld[2] = {10, 20};
    Vect@int v = {d, 3};
    Vect@long lv = {ld, 2};

    if ((&v)->vat(1) != 2)
        err(1, "int 实例的方法不对");
    if ((&lv)->vat(0) != 10)
        err(2, "long 实例的方法不对");
    if ((&v)->vsum() != 6 || (&lv)->vsum() != 30)
        err(3, "模板体里再调用模板不对");
    if ((&v)->last() != 3)
        err(4, "last() 不对");
}
test "显式应用和接收者写法是同一个函数" {
    int d[2] = {7, 8};
    Vect@int v = {d, 2};
    int (*fp)(Vect@int *, int) = vat@int;
    int (*fp2)(Vect@int *, int) = vat@int;
    long (*fpl)(Vect@long *, int) = vat@long;

    if (vat@int(&v, 1) != (&v)->vat(1))
        err(1, "两种写法结果不同");
    if (fp != fp2)
        err(2, "同一个实参应当只有一份实例");
    if ((void *)fp == (void *)fpl)
        err(3, "不同实参应当是不同函数");
    if (fp(&v, 0) != 7)
        err(4, "函数指针调用不对");

    push@int(&v, 9);
    (&v)->push(10);
    if (v.len != 4 || v.data[3] != 10)
        err(5, "push 实例不对");
}
test "普通函数模板：实参写在名字后面" {
    if (id@int(3) != 3)
        err(1, "id@int 不对");
    if (id@long(4L) != 4)
        err(2, "id@long 不对");
    if (max2@int(3, 9) != 9 || max2@int(9, 3) != 9)
        err(3, "max2@int 不对");
    if (max2@long(-1L, -2L) != -1)
        err(4, "max2@long 不对");
}
test "多形参模板：|K, V| 配 @(K, V)" {
    Map@(char *, int) m = {"hi", 7};

    if (second@(char *, int)(&m) != 7)
        err(1, "second 实例不对");
    if ((&m)->second() != 7)
        err(2, "多形参方法模板不对");
}
test "声明时接收者的模板还没写出来也可以" {
    Later@char l = {'z'};

    if (only@char(&l) != 'z' || (&l)->only() != 'z')
        err(1, "后写出来的接收者模板不对");
}
test "typedef 模板给出实例的别名" {
    int d[2] = {1, 2};
    MyVec@int mv = {d, 2};
    Box@char b = {'q', 5};
    Ptr@int p = d;

    if ((&mv)->vsum() != 3)
        err(1, "MyVec@int 不是 Vect@int");
    if (sizeof(MyVec@int) != sizeof(Vect@int))
        err(2, "别名和实例的大小应当一致");
    if (b.v != 'q' || b.n != 5)
        err(3, "匿名结构 typedef 模板不对");
    if (sizeof(Box@long) < sizeof(long))
        err(4, "Box@long 太小");
    if (*p != 1)
        err(5, "Ptr@int 不是 int *");
    if (sizeof(Ptr@char) != sizeof(char *))
        err(6, "Ptr@char 不是 char *");
}
test "切片可以作形参" {
    int ia[3] = {1, 2, 3};
    char cs[2] = {'a', 'b'};
    int[:] is = ia;
    char[:] csl = cs;

    if (nel@int(is) != 3)
        err(1, "int 切片实参不对");
    if (nel@char(csl) != 2)
        err(2, "char 切片实参不对");
}
/* 裸机上没有 pthread：`go` 起不了线程，`h->join()` 走 libtcc1 的每线程块，
 * 任务收尾会三重故障（见 README「把 tests/ 也搬上裸机」），所以这一块不进
 * 裸机那张表，同一个文件里其它块照跑。 */
// BARE-SKIP
test "实例是普通函数：可以起线程" {
    int d[3] = {4, 5, 6};
    Vect@int v = {d, 3};
    thread h = go vsum@int(&v);
    thread h2 = go vat@int(&v, 2);

    if (h->join() != 0)
        err(1, "join 失败");
    if ((int)h->value() != 15)
        err(2, "线程里的实例调用不对");
    if (h2->join() != 0)
        err(3, "join 失败");
    if ((int)h2->value() != 6)
        err(4, "线程里的显式应用不对");
}
test "可失败的模板用 catch 收尾" {
    int d[2] = {3, 4};
    Vect@int v = {d, 2};

    if (get_or(&v, 1) != 4)
        err(1, "成功路径不对");
    if (get_or(&v, 9) != -1)
        err(2, "catch 没有接住模板里的 err");
    if (get_or(&v, -1) != -1)
        err(3, "负下标没有走失败路径");
}

int main(void)
{
    int d[2] = {1, 2};
    Vect@int v = {d, 2};

    printf("generic_fn: %d %d\n", (&v)->vsum(), id@int(7));
    return 0;
}
