/* C99 输出后端自测：同一份源码先由 tcc 前端（c6 模式）编译成 C99 源码，
 * 再用系统 gcc 编译、运行，检查生成的后端产物真的能工作。
 *
 * 运行（仓库根目录）：
 *   make c99test
 * 或分步手工跑：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/c99/c99test.c
 *   ./tcc/tcc -std=c6 -B tcc -o /tmp/c99test.c tests/c99/c99test.c
 *   gcc -std=c99 -pedantic -Wall -Wextra -Werror -pthread \
 *       -o /tmp/c99test /tmp/c99test.c
 *   /tmp/c99test
 *
 * 这里刻意混用普通 C 与 c6 特性，覆盖 README 里 C99 后端承诺的范围：
 * 整数/浮点/指针/数组/字符串、控制流、函数指针与递归、结构体按值返回、
 * defer/errdefer/err/try/catch、切片/foreach、元组、方法/接口/裸 tag、
 * async/await 与 go 的运行时。
 */
#include <stdio.h>
#include <string.h>

// OUTPUT c99test: ok

/* c6 错误槽：catch 之后应当已经清空 */
int __tcc_err_flag_get(void);

/* ---- 普通 C 的辅助函数 ---- */

static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static int apply(int (*fn)(int, int), int a, int b) { return fn(a, b); }

static void swap(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

struct Point { int x, y; };
struct Big { long a, b, c; };

static struct Point make_point(int x, int y)
{
    struct Point p = { x, y };
    return p;
}

static struct Big make_big(long a, long b, long c)
{
    struct Big v = { a, b, c };
    return v;
}

static int point_sum(struct Point p) { return p.x + p.y; }
static long big_sum(struct Big v) { return v.a + v.b + v.c; }

static int (*g_op)(int, int) = add;

/* ---- c6 的辅助函数 ---- */

static char logbuf[64] = {0};
static int loglen = 0;

static void log_reset(void)
{
    loglen = 0;
    logbuf[0] = '\0';
}

static void lg(const char *s)
{
    while (*s && loglen < (int)sizeof(logbuf) - 1)
        logbuf[loglen++] = *s++;
    logbuf[loglen] = '\0';
}

#define LOG_IS(s) (0 == strcmp(logbuf, (s)))

static int last_id = 0;
static char last_msg[32] = {0};

static void save_msg(const char *s)
{
    int i = 0;

    if (!s)
        s = "";
    while (s[i] && i < (int)sizeof(last_msg) - 1) {
        last_msg[i] = s[i];
        i++;
    }
    last_msg[i] = '\0';
}

static int!error fetch(int n)
{
    errdefer lg("E");
    if (n < 0)
        err(7, "negative");
    lg("N");
    return n * 2;
}

static int!error fetch_twice(int n)
{
    return (try fetch(n)) + 1;
}

static int probe(int n)
{
    int v = 0;

    v = fetch(n) catch e {
        last_id = e.id;
        save_msg(e.msg);
        return -1;
    };
    return v;
}

static int probe_twice(int n)
{
    int v = 0;

    v = fetch_twice(n) catch e {
        last_id = e.id;
        save_msg(e.msg);
        return -1;
    };
    return v;
}

static int sum_slice(int[:] s)
{
    int total = 0;
    int i = 0;

    for (i = 0; i < (int)s.len; i++)
        total += s[i];
    return total;
}

static (int, int) divmod(int a, int b)
{
    return { a / b, a % b };
}

struct Vec { int x, y; };
int vec_sum(Vec *self) { return self->x + self->y; }

interface Num { int num_twice(); };
struct Number { int v; };
int num_twice(Number *self) { return self->v * 2; }

async int async_add(int a, int b) { return a + b; }

static volatile int go_flag = 0;

static int set_flag(int v)
{
    go_flag = v;
    return 0;
}

/* ---- 测试块 ---- */

test "整数、位运算与整型宽度" {
    int a = 7, b = 3;
    unsigned u = 0xF0u;
    long long big = 1LL << 40;

    if (a / b != 2 || a % b != 1)
        err(1, "整数除/模不对");
    if ((a << 3) != 56 || (a >> 1) != 3)
        err(2, "移位不对");
    if ((a & b) != 3 || (a | b) != 7 || (a ^ b) != 4)
        err(3, "位运算不对");
    if ((u >> 4) != 0x0Fu || (~0u & 0xFFu) != 0xFFu)
        err(4, "无符号运算不对");
    if (big != 1099511627776LL)
        err(5, "long long 不对");
    if (sizeof(char) != 1 || sizeof(short) != 2 || sizeof(int) != 4
        || sizeof(long long) != 8)
        err(6, "整型宽度不对");
}

test "浮点与类型转换" {
    double d = 1.5, e = 2.25;
    float f = 0.5f;
    int i = (int)(d * 4.0);
    double q = (double)7 / 2;

    if (d + e != 3.75)
        err(1, "double 加法不对");
    if (f * 8.0f != 4.0f)
        err(2, "float 乘法不对");
    if (i != 6)
        err(3, "double -> int 转换不对");
    if (q != 3.5 || (int)q != 3)
        err(4, "整数除法/截断不对");
    if (!(d < e) || !(e > (double)f))
        err(5, "浮点比较不对");
    if (-d + 0.5 != -1.0)
        err(6, "一元负号不对");
    if ((double)((int)3.9) != 3.0)
        err(7, "浮点截断不对");
}

test "指针、数组与字符串" {
    int a[5] = { 1, 2, 3, 4, 5 };
    int *p = a;
    int total = 0;
    int i = 0;
    char buf[16] = "hello";
    char *s = buf;
    const char *lit = "c99";

    for (i = 0; i < 5; i++)
        total += p[i];
    if (total != 15)
        err(1, "数组求和不对");
    if (*(p + 2) != 3 || p[4] != 5)
        err(2, "指针运算不对");
    swap(&a[0], &a[4]);
    if (a[0] != 5 || a[4] != 1)
        err(3, "指针传参没有写回");
    if (strlen(s) != 5 || strcmp(s, "hello") != 0)
        err(4, "字符串常量不对");
    s[0] = 'H';
    if (buf[0] != 'H')
        err(5, "字符数组写入不对");
    if (strlen(lit) != 3 || lit[2] != '9')
        err(6, "字符串字面量不对");
    if ((int)(sizeof(a) / sizeof(a[0])) != 5)
        err(7, "sizeof 数组不对");
}

test "控制流：if/while/do/for/switch" {
    int i = 0, n = 0, sum = 0;

    for (i = 0; i < 10; i++) {
        if (i == 2)
            continue;
        if (i == 8)
            break;
        sum += i;
    }
    if (sum != 26)
        err(1, "for + break/continue 不对");

    i = 0;
    n = 0;
    while (i < 3) {
        n++;
        i++;
    }
    do {
        n++;
    } while (0);
    if (n != 4)
        err(2, "while/do-while 不对");

    n = 0;
    switch (3) {
    case 1:
        n = 1;
        break;
    case 3:
        n += 10;
        /* fall through */
    case 4:
        n += 1;
        break;
    default:
        n = -1;
    }
    if (n != 11)
        err(3, "switch fallthrough 不对");

    n = 0;
    switch (99) {
    case 1:
        n = 1;
        break;
    default:
        n = 2;
    }
    if (n != 2)
        err(4, "switch default 不对");

    if (!(1 && 2) || (0 || 0) || (1 ? 5 : 6) != 5)
        err(5, "逻辑/三目运算不对");

    sum = 0;
    i = 0;
again:
    sum += i++;
    if (i < 3)
        goto again;
    if (sum != 3)
        err(6, "goto 不对");
}

test "函数指针、递归与间接调用" {
    int (*op)(int, int) = mul;

    if (apply(op, 6, 7) != 42)
        err(1, "局部函数指针不对");
    if (g_op(20, 22) != 42)
        err(2, "全局函数指针初值不对");
    g_op = mul;
    if (g_op(6, 7) != 42)
        err(3, "全局函数指针赋值不对");
    if (apply(add, 1, 2) != 3)
        err(4, "直接传函数名不对");
    if (fib(10) != 55)
        err(5, "递归不对");
}

test "结构体、联合体与按值返回" {
    struct Point p = make_point(20, 22);
    struct Big b = make_big(1, 2, 39);
    struct Point q = { 0 };
    union U { int i; char c; } u = { 0 };
    struct Point arr[3] = { { 1, 2 }, { 3, 4 }, { 5, 6 } };

    if (p.x != 20 || p.y != 22 || point_sum(p) != 42)
        err(1, "小结构体按值传递/返回不对");
    if (big_sum(b) != 42 || b.c != 39)
        err(2, "大结构体按值传递/返回不对");
    q = p;
    q.x = 1;
    if (p.x != 20 || q.x != 1)
        err(3, "结构体赋值不是值语义");
    u.i = 0x41424344;
    if (u.i != 0x41424344)
        err(4, "联合体成员读写不对");
    if ((int)sizeof(struct Big) != 24)
        err(5, "结构体 sizeof 不对");
    if (arr[1].x != 3 || arr[2].y != 6)
        err(6, "结构体数组不对");
}

test "defer / errdefer / err / try / catch" {
    int v = 0;

    log_reset();
    {
        defer lg("d1");
        defer lg("d2");
        lg("body");
    }
    if (!LOG_IS("bodyd2d1"))
        err(1, "defer 顺序不对");

    log_reset();
    v = (try fetch(5));
    if (v != 10 || !LOG_IS("N"))
        err(2, "成功路径的 try 或 errdefer 不对");

    log_reset();
    v = probe(-1);
    if (v != -1 || last_id != 7 || strcmp(last_msg, "negative") != 0)
        err(3, "catch 没有拿到错误码/消息");
    if (!LOG_IS("E"))
        err(4, "失败路径没有执行 errdefer");
    if (__tcc_err_flag_get())
        err(5, "catch 之后错误槽没有清空");

    last_id = 0;
    v = probe_twice(-2);
    if (v != -1 || last_id != 7)
        err(6, "错误没有穿过 try 传播");
    v = probe_twice(20);
    if (v != 41)
        err(7, "传播时的成功路径不对");
}

test "切片与 foreach" {
    int a[5] = { 1, 2, 3, 4, 5 };
    int[:] s = a[1:4];
    int total = 0, count = 0;

    if ((int)s.len != 3 || sum_slice(s) != 9)
        err(1, "切片长度或元素不对");
    s[0] = 20;
    if (a[1] != 20)
        err(2, "通过切片写入没有落到数组");

    foreach |i, v| a {
        if ((int)i != count)
            err(3, "foreach 下标不连续");
        total += v;
        count++;
    }
    if (count != 5 || total != 1 + 20 + 3 + 4 + 5)
        err(4, "数组 foreach 不对");

    total = 0;
    foreach |i| 0:4 {
        total += (int)i;
    }
    if (total != 6)
        err(5, "区间 foreach 不对");

    total = 0;
    foreach |_, v| s {
        total += v;
    }
    if (total != 27)
        err(6, "切片 foreach 不对");
}

test "元组" {
    (int, int) t = divmod(42, 5);
    (char *, int) g = { "hello", 5 };

    if (t[0] != 8 || t[1] != 2)
        err(1, "元组返回值不对");
    if ((int)sizeof(t) != 8)
        err(2, "元组 sizeof 不对");
    if (strcmp(g[0], "hello") != 0 || g[1] != 5)
        err(3, "带指针的元组不对");
    t[0] = 9;
    if (t[0] != 9)
        err(4, "元组元素赋值不对");
}

test "方法、接口与裸 tag 类型名" {
    Vec v = { 3, 4 };
    Vec *p = &v;
    Number n = { 21 };
    Num num = n;

    if (p->vec_sum() != 7)
        err(1, "方法调用不对");
    if (vec_sum(p) != 7)
        err(2, "方法当普通函数调用不对");
    if (num.num_twice() != 42)
        err(3, "接口方法调用不对");
}

test "async / await 与 go 的运行时" {
    struct async_add_task *t = async_add(20, 22);

    if (__c6_co_wait(t) != 0)
        err(1, "协程驱动失败");
    if (t->val != 42)
        err(2, "async 返回值不对");
    __c6_co_destroy(t);

    go_flag = 0;
    go set_flag(1);
    __c6_th_join_all();
    if (go_flag != 1)
        err(3, "go 线程没有跑起来");
}

test "c99 输出后端端到端标记" {
    printf("c99test: ok\n");
}
