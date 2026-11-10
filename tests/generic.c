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

/* 值形参：写成 C 参数表形状，按常量绑定，所以能当数组长度 */
struct Buf@(T, int N) { T data[N]; int len; };

/* 只有值形参的模板，实例名带上实参的拼写：'Step@(-3)' */
struct Step@(int N) { int step; };

/* 实例就是普通结构体，方法照常写：接收者写成 Vect@int */
int vect_sum(Vect@int *v)
{
    int i = 0, s = 0;
    for (i = 0; i < v->len; i++)
        s += v->data[i];
    return s;
}

/* 只有值形参的模板：实例之间只差常量 */
struct Arr@(int N) { int d[N]; };

/* 值形参当位域宽度 */
struct Flags@(T, int B) { T on : B; T off : B; };

/* 体内成员函数（V3）：方法直接写在模板体里，接收者就是隐式的 'self'，
   所以字段按名字读写，方法按名字调用——和普通 C++ 风格的方法一个读法 */
struct Vec2@T {
    T x, y;
    T sum2(void) { return x + y; }
    void scale(T k) { x = x * k; y = y * k; }
    T dot(Vec2@T *o) { return x * o->x + y * o->y; }
    T dbl(void) { return sum2() * 2; }   /* 调用同模板里的另一个方法 */
};

/* 体内方法也能用值形参，也能先用后定义 */
struct Stack@(T, int N) {
    T data[N];
    int len;
    int cap(void) { return N; }
    int room(void) { return cap() - len; }   /* cap 先写，room 后用 */
    int push(T v) {
        if (len >= N)
            return 0;
        data[len] = v;
        len = len + 1;
        return 1;
    }
    T pop(void) {
        len = len - 1;
        return data[len];
    }
};

/* 先写的方法调用后写的方法：实例化时先发全部原型，函数体留到单元末尾 */
struct Chain@T {
    T v;
    T doubled(void) { return step() * 2; }
    T step(void) { return v; }
};

/* 模板里再嵌实例：隐式成员就是那个实例自己 */
struct VecBox@T {
    Vect@T v;
    int count(void) { return v.len; }
    T first(void) { return v.data[0]; }
};

/* 体内方法的符号名和 '|T|' 方法一致：'<实例标签>_<方法名>'。实例先把
   别名造出来，下面就能按拼出来的名字声明同一个函数 */
/* 体内方法的形参表：'(void)'、'()' 只剩接收者，'void *p' 要留住那个 void */
struct Sig@T {
    T v;
    int none(void) { return v; }
    int noargs() { return v + 1; }
    int raw(void *p) { return p != 0; }
    int two(const int a, const int b) { return a + b; }
};
/* 托管引用 + 体内方法：接收者是 '@Holder@int'，方法把托管字段当普通字段用 */
struct Ref { int v; };
struct Keeper@T {
    @Ref r;
    void keep(@Ref p) { r = p; }
    @Ref held(void) { return r; }
    int rc_after(void) { return __c6_gc_refcount(r); }
};
typedef Stack@(int, 4) IntStack4;
int Stack_int_4_cap(struct Stack_int_4 *);
int Stack_int_4_room(struct Stack_int_4 *);

/* 函数模板也可以只有值形参，用在常量表达式里 */
|int N| int total(int *d)
{
    int i = 0, s = 0;

    for (i = 0; i < N; i++)
        s += d[i];
    return s;
}

/* 形参表里有值形参时，方法模板照样一份覆盖所有实例 */
|T, int N| int btotal(Buf@(T, N) *self)
{
    int i = 0, s = 0;

    for (i = 0; i < self->len; i++)
        s += (int)self->data[i];
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

test "值形参当数组长度" {
    Buf@(int, 4) b = {{1, 2, 3, 4}, 4};
    Buf@(char, 12) c = {{'x'}, 1};

    if (sizeof(Buf@(int, 4)) != 4 * sizeof(int) + sizeof(int))
        err(1, "值形参的布局不对");
    if (sizeof(Buf@(char, 12)) != 12 + sizeof(int))
        err(2, "另一个值实参应当是另一个实例");
    if (b.data[3] != 4 || c.data[0] != 'x' || b.len != 4)
        err(3, "值形参实例的内容不对");
}
test "值实参可以是常量表达式（含负数）" {
    Step@(2 + 1) st = {7};
    Step@(-3) neg = {-7};

    if (st.step != 7 || neg.step != -7)
        err(1, "值实参不对");
    if (sizeof(Step@(2 + 1)) != sizeof(Step@(-3)))
        err(2, "同一个模板的实例布局应当一样");
}
test "只有值形参的模板、位域宽度" {
    int raw[4] = {1, 2, 3, 4};
    Arr@(4) a = {{9, 8, 7, 6}};
    Flags@(unsigned, 3) f = {5, 2};

    if (total@(4)(raw) != 10 || total@(2 + 2)(raw) != 10)
        err(1, "值形参的函数模板不对");
    if (sizeof(Arr@(4)) != 4 * sizeof(int) || a.d[3] != 6)
        err(2, "只有值形参的模板不对");
    if (f.on != 5 || f.off != 2)
        err(3, "值形参当位域宽度不对");
}
test "值形参在方法模板里" {
    Buf@(int, 4) b = {{1, 2, 3, 4}, 4};

    if ((&b)->btotal() != 10)
        err(1, "值形参的方法模板不对");
    if (btotal@(int, 4)(&b) != 10)
        err(2, "显式写法和接收者写法应当一样");
}

test "体内方法：字段和参数直接读写" {
    Vec2@int a = {3, 4};
    Vec2@int b = {10, 20};
    Vec2@int *p = &a;

    if (p->sum2() != 7)
        err(1, "体内方法读字段不对");
    p->scale(2);
    if (a.x != 6 || a.y != 8)
        err(2, "体内方法写字段不对");
    if (p->dot(&b) != 6 * 10 + 8 * 20)
        err(3, "体内方法收别人的实例不对");
    if (p->dbl() != 28)
        err(4, "体内方法调用别的方法不对");
    if ((&a)->sum2() != 14)
        err(5, "结构体值上调用体内方法不对");
}
test "体内方法用值形参、先写的方法调用后写的" {
    Stack@(int, 4) s = {{0}, 0};
    Stack@(int, 4) *sp = &s;
    Chain@int c = {21};
    Chain@int *cp = &c;

    if (sp->cap() != 4 || sp->room() != 4)
        err(1, "值形参在体内方法里不对");
    if (!sp->push(11) || !sp->push(22) || s.len != 2)
        err(2, "push 不对");
    if (sp->room() != 2 || sp->pop() != 22 || s.len != 1)
        err(3, "pop 不对");
    if (cp->doubled() != 42)
        err(4, "调用后定义的方法不对");
}
test "体内方法的符号和 '|T|' 方法一样" {
    IntStack4 s = {{1, 2, 3, 4}, 4};

    if (Stack_int_4_cap(&s) != 4)
        err(1, "实例标签_方法名 不是普通符号");
    if (Stack_int_4_room(&s) != 0)
        err(2, "直接调用体内方法不对");
    if ((&s)->cap() != 4)
        err(3, "两种调用写法结果应当一样");
}
test "体内方法的形参表：void、空表、const" {
    Sig@int s = {7};
    int k = 0;

    if ((&s)->none() != 7)
        err(1, "'(void)' 只剩接收者");
    if ((&s)->noargs() != 8)
        err(2, "'()' 只剩接收者");
    if ((&s)->raw(&k) != 1 || (&s)->raw(0) != 0)
        err(3, "'void *p' 的 void 被吃掉了");
    if ((&s)->two(2, 3) != 5)
        err(4, "'const int' 形参不对");
}
test "托管实例上的方法不碰引用计数" {
    /* 接收者只是被拷进一个临时槽里，那份拷贝是借用：不该加计数，
       也不该去释放临时槽里的旧内容 */
    @Keeper@int h = new Keeper@int;
    int i = 0;

    h->keep(new Ref);
    if (h->rc_after() != 1)
        err(1, "托管字段自己的计数不是 1");
    for (i = 0; i < 4; i++)
        if (h->rc_after() != 1)
            err(2, "每调一次方法就漏一个引用");
    if (h->held() == NULL)
        err(3, "托管字段读不出来");
}
test "体内方法里的实例再实例" {
    int d[3] = {5, 6, 7};
    Vect@int inner = {d, 3};
    VecBox@int b = {inner};
    VecBox@int *bp = &b;
    if (bp->count() != 3)
        err(1, "实例成员不对");
    if (bp->first() != 5)
        err(2, "实例成员的数组不对");
}

int main(void)
{
    int d[1] = {1};
    Vect@int v = {d, 1};

    printf("generic: %d %d\n", (&v)->vect_sum(), (int)sizeof(Vect@long));
    return 0;
}
