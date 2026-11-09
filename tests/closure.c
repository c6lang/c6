/* c6 V2：捕获式闭包 closure RET (参数) 的运行时自测
 *
 * 运行（仓库根目录）：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/closure.c
 */
#include <stdio.h>

struct Point { int x, y; };
struct Node { int v; };

static int bump(int x) { return x + 1; }
static int call_closure(closure int(int) f, int x) { return f(x); }

static closure int(int) make_adder(int n)
{
    return int(int x) { return x + n; };
}

test "closure 类型：大小、typedef、零值" {
    closure int(int) f = 0;
    typedef closure int(int) F;
    F g = 0;
    if (sizeof(f) != sizeof(void *))
        err(1, "closure 不是单字");
    if (f != 0 || g != 0)
        err(2, "空闭包不是 NULL");
    (void)f;
    (void)g;
}

test "标量捕获：快照、可写副本、多次调用保留状态" {
    int n = 1, m = 10;
    closure int() f = int() { n = n + 1; m = m + 2; return n + m; };
    int a = f();
    int b = f();
    int outer = n + m;
    f = NULL;
    if (a != 14 || b != 17 || outer != 11)
        err(1, "标量捕获的状态不对");
}

test "形参捕获与返回闭包" {
    closure int(int) add5 = make_adder(5);
    if (add5(1) != 6 || add5(10) != 15)
        err(1, "返回的闭包丢了环境");
    add5 = NULL;
}

test "结构体捕获：体里改的是 env 副本" {
    struct Point p = { 3, 4 };
    closure int() f = int() { p.x++; return p.x + p.y; };
    int a = f();
    int b = f();
    f = NULL;
    if (a != 8 || b != 9 || p.x != 3)
        err(1, "结构体副本不对");
}

test "切片与函数指针捕获" {
    int a[2] = { 7, 8 };
    int[:] s = a;
    int (*fp)(int) = 0;
    closure int() f = int() { return s[0] + (fp != 0); };
    int r = f();
    f = NULL;
    if (r != 7)
        err(1, "切片捕获不对");
}

test "托管捕获：闭包持有 @T 的强引用" {
    @Node p = new Node;
    p->v = 42;
    {
        closure int() f = int() { return p->v; };
        int r = f();
        f = NULL;
        if (r != 42)
            err(1, "托管捕获的值不对");
    }
    p = NULL;
    if (__c6_gc_live() != 0)
        err(2, "托管捕获没有释放");
}

test "sizeof 捕获：数组只做类型检查" {
    int a[4] = {0};
    int n = 1;
    closure int() f = int() { return (int)sizeof(a) + n; };
    int r = f();
    f = NULL;
    if (r != (int)sizeof(a) + 1)
        err(1, "sizeof 捕获不对");
}

test "遮蔽：体局部遮蔽外层捕获" {
    int n = 1, m = 5;
    closure int() f = int() { int n = 2; return n + m; };
    int r = f();
    f = NULL;
    if (r != 7 || n != 1)
        err(1, "遮蔽不对");
}

test "函数指针转闭包：形参、返回值、实参" {
    closure int(int) f = bump;
    closure int(int) g = bump;
    int a = f(4);
    int b = call_closure(g, 9);
    f = NULL;
    g = NULL;
    if (a != 5 || b != 10)
        err(1, "函数指针转闭包不对");
}

test "defer 与闭包捕获" {
    int n = 1;
    closure int() f = int() { defer n = n + 2; return n; };
    int a = f();
    int b = f();
    f = NULL;
    if (a != 1 || b != 3 || n != 1)
        err(1, "defer 捕获不对");
}
