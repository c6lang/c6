/* c6 自测：泛型 'struct Vect@T { ... }' / 'Vect@int'
 *
 * 模板不是类型：写模板的地方不布局任何东西，名字只在带实参时才是类型
 * （裸名字报 C103）。'Vect@int' 会按实参拼出实例名 'Vect_int'，把形参
 * 绑成普通 typedef，再把模板体当作普通 struct 重新解析一遍——所以布局、
 * 方法、接口、切片、元组和后端都只看到普通结构体。
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/generic.c
 * 或：
 *   make selftest
 */
#include <stdio.h>
#include <string.h>

/* ---- 被测的东西 ---- */

struct Vect@T { T *data; int len; };

struct Map@(K, V) { K key; V val; };

/* 模板可以提到自己：参数 T 在这一层已经绑好了 */
struct Node@T { T val; Node@T *next; };

/* 模板里可以再放实例 */
struct Holder@T { Vect@T v; T extra; };

struct Empty@T { T only; };

/* 实例就是普通结构体，方法照常写：接收者写成 Vect@int */
int vect_sum(Vect@int *v)
{
    int i = 0, s = 0;
    for (i = 0; i < v->len; i++)
        s += v->data[i];
    return s;
}

/* 别名和实例是同一个类型 */
typedef Vect@int IntVect;

int iv_first(IntVect *v) { return v->data[0]; }

Vect@int make(int *d, int n) { Vect@int v = {d, n}; return v; }
long vect_sum_long(Vect@long *v) { return v->data[0] + v->data[1]; }

/* 文件作用域的实例变量 */
int g_a[3] = {1, 2, 3};
Vect@int g_vec = {g_a, 3};
int g_pairs[2] = {4, 5};

test "单形参实例的字段就是绑定的类型" {
    int d[2] = {7, 9};
    Vect@int v = {d, 2};

    if (sizeof(v.data) != sizeof(int *))
        err(1, "data 不是 int *");
    if (v.len != 2 || v.data[1] != 9)
        err(2, "字段值不对");
    if (sizeof(Vect@int) < sizeof(int *) + sizeof(int)
        || sizeof(Vect@int) % sizeof(int *) != 0)
        err(3, "布局不对");
}
test "不同实参是不同的类型" {
    long ld[2] = {1, 2};
    Vect@long lv = {ld, 2};

    if (sizeof(Empty@int) == sizeof(Empty@long))
        err(1, "long 实例和 int 实例大小应当不同");
    if (sizeof(Empty@char) != sizeof(char))
        err(2, "char 实例应当只有一个 char");
    if (vect_sum_long(&lv) != 3)
        err(3, "long 实例的字段不对");
}
test "两个实参要写括号，顺序不能错" {
    Map@(char *, int) m = {"hi", 7};

    if (strcmp(m.key, "hi") != 0 || m.val != 7)
        err(1, "多形参实例字段不对");
    if (sizeof(m.key) != sizeof(char *))
        err(2, "K 应当绑成 char *");
}
test "模板可以自引用" {
    Node@int n2 = {2, 0};
    Node@int n1 = {1, &n2};
    Node@int n0 = {0, &n1};
    Node@int *p = &n0;
    int sum = 0;

    while (p) {
        sum += p->val;
        p = p->next;
    }
    if (sum != 3)
        err(1, "链表遍历不对");
}
test "嵌套应用：Vect@Vect@int" {
    int da[2] = {1, 2}, db[2] = {3, 4};
    Vect@int va = {da, 2}, vb = {db, 2};
    Vect@(Vect@int) vv = {0, 0};

    vv.data = &va;
    vv.len = 2;
    if (vv.data[0].data[1] != 2 || vv.len != 2)
        err(1, "嵌套实例不对");
    if (sizeof(Empty@(Empty@int)) != sizeof(int))
        err(2, "嵌套实例大小不对");
    (void)vb;
}
test "模板里可以再放实例" {
    char d[1] = {5};
    Holder@char h = {{d, 1}, 'z'};

    if (h.v.data[0] != 5 || h.extra != 'z')
        err(1, "模板里嵌实例不对");
    if (sizeof(h) != sizeof(Vect@char) + sizeof(char *))
        err(2, "嵌套布局不对");
}
test "实例就是普通结构体，方法按接收者定类型" {
    int d[3] = {1, 2, 3};
    Vect@int v = make(d, 3);

    if ((&v)->vect_sum() != 6)
        err(1, "方法调用不对");
    if ((&v)->iv_first() != 1)
        err(2, "别名接收者的方法不对");
}
test "实例可以作返回值、参数和文件作用域变量" {
    if ((&g_vec)->vect_sum() != 6)
        err(1, "文件作用域实例不对");
}
test "重复应用同一个实参共享同一个类型" {
    int da[1] = {1}, db[1] = {2};
    Vect@int a = {da, 1};
    Vect@int b = {db, 1};
    Vect@int *p = &b;

    *p = a;
    if (p->data[0] != 1)
        err(1, "同一个实例的赋值不对");
    if (sizeof(Vect@int) != sizeof(a))
        err(2, "同一个实例的 sizeof 应当一致");
}
test "切片的元素可以是实例" {
    int da[1] = {1}, db[1] = {2};
    Vect@int arr[2] = {{da, 1}, {db, 1}};
    Vect@int[:] s = arr;
    int sum = 0;

    foreach |i, e| s {
        sum += e.data[0];
    }
    if (sum != 3)
        err(1, "实例切片不对");
    if (s[1].data[0] != 2)
        err(2, "下标不对");
}
test "元组的元素可以是实例" {
    Vect@int v = {g_pairs, 2};
    (Vect@int, int) t = {v, 8};

    if (t[0].data[1] != 5 || t[1] != 8)
        err(1, "元组里放实例不对");
}
test "实例类型可以在函数体内第一次产生" {
    struct Local@U { U u; int n; };
    Local@short l = {3, 4};

    if (l.u != 3 || l.n != 4)
        err(1, "函数体内定义的模板不对");
    if (sizeof(Local@short) != sizeof(int) + sizeof(int))
        err(2, "函数体内实例的布局不对");
}
test "指针和数组类型可以作实参（要括号）" {
    int x = 1, y = 2;
    Map@(int *, int *) m = {&x, &y};

    *m.key = 10;
    if (x != 10 || *m.val != 2)
        err(1, "指针实参不对");
}
test "空的实例也照常工作" {
    Empty@int e = {7};

    if (e.only != 7)
        err(1, "单字段实例不对");
    if (sizeof(Empty@long) != sizeof(long))
        err(2, "单字段实例应当和字段一样大");
}

int main(void)
{
    int d[1] = {1};
    Vect@int v = {d, 1};

    printf("generic: %d %d\n", (&v)->vect_sum(), (int)sizeof(Vect@long));
    return 0;
}
