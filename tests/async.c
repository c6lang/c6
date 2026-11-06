/* c6 自测：async / await 无栈协程
 *
 * 'async' 函数被编译成 'int __c6_body_NAME(void *task)'，'await' 处保存
 * 已用的栈帧并回到驱动方，恢复时把帧拷回同样的偏移再跳到恢复标签。这里
 * 检查返回值、挂起/恢复后的局部变量、defer 与挂起的关系、以及任务槽的
 * 回收。
 *
 * 运行（仓库根目录）：
 *   make selftest
 * 或单个文件：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/async.c
 */
#include <stdio.h>
#include <string.h>

/* 只用到这三个 libc 函数（不给 libtcc1 添依赖） */
int pipe(int *fds);
int read(int fd, void *buf, int len);
int write(int fd, const void *buf, int len);

/* ---- 日志（按执行顺序拼接） ---- */
static char logbuf[128] = {0};
static int loglen = 0;

static void lg_reset(void)
{
    loglen = 0;
    logbuf[0] = '\0';
}

static void lg(const char *s)
{
    while (*s && loglen < (int)sizeof(logbuf) - 2)
        logbuf[loglen++] = *s++;
    logbuf[loglen] = '\0';
}

#define LG_IS(s) (0 == strcmp(logbuf, (s)))

static void lgc(char c)
{
    if (loglen < (int)sizeof(logbuf) - 2) {
        logbuf[loglen++] = c;
        logbuf[loglen] = '\0';
    }
}

/* ---- 被测的 async 函数 ---- */

async int add(int a, int b)
{
    return a + b;
}

async int connect(void)
{
    lg("C");
    return 0;
}

async int send(void)
{
    lg("S");
    return 0;
}

async int recv(void)
{
    lg("R");
    return 7;
}

/* 讨论里关心的三种写法：语句式 await、语句式 await、赋值式 await */
async int session(void)
{
    int got = 0;
    await connect();
    await send();
    got = await recv();
    return got;
}

/* 挂起前写入的局部变量，恢复后必须还在 */
async int locals_alive(void)
{
    int a = 11;
    int b = 22;
    char buf[8] = "hi";
    a = await add(a, 0);
    b = await add(b, 0);
    return a * 100 + b + (int)strlen(buf);
}

/* 一次 await 里嵌套另一个 await（参数里先算） */
async int nested(void)
{
    return await add(await add(1, 2), 4);
}

/* defer 注册在挂起之前：记录要被 park/restore，退出时按逆序执行 */
async int with_defer(void)
{
    int tag = 1;
    defer lg("d1");
    defer lg("d2");
    tag = await add(2, 3);
    lg("body");
    return tag;
}

/* void 的 async 也能 await */
async void fire(void)
{
    lg("f");
}

async int fire_user(void)
{
    await fire();
    return 1;
}

/* await c6_io：真正的挂起原语。fd 1 一直可写，select 会立刻返回 */
async int io_rounds(void)
{
    int n = 0;
    int rev = 0;
    while ((rev = await c6_io(1, C6_CO_EV_WRITE)) > 0 && n < 3)
        n++;
    return n;
}

/* 循环里的 await：同一个恢复标签被反复命中 */
async int loop_sum(int count)
{
    int i = 0;
    int sum = 0;
    for (i = 0; i < count; i++)
        sum = await add(sum, i);
    return sum;
}

/* ---- 测试 ---- */

test "await 一个 async 调用拿到返回值" {
    struct add_task *t = add(20, 22);
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 42)
        err(2, "返回值不对");
    __c6_co_destroy(t);
}

test "await connect(); await send(); a = await recv(); 三种写法" {
    struct session_task *t = NULL;
    lg_reset();
    t = session();
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 7)
        err(2, "await 的返回值没传到上一层");
    if (!LG_IS("CSR"))
        err(3, "三个 async 调用的执行顺序不对");
    __c6_co_destroy(t);
}

test "挂起不破坏局部变量" {
    struct locals_alive_task *t = locals_alive();
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 11 * 100 + 22 + 2)
        err(2, "恢复后的局部变量不对");
    __c6_co_destroy(t);
}

test "await 可以嵌套在参数里" {
    struct nested_task *t = nested();
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 7)
        err(2, "嵌套 await 的结果不对");
    __c6_co_destroy(t);
}

test "循环里的 await 反复命中同一个恢复点" {
    struct loop_sum_task *t = loop_sum(5);
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 0 + 1 + 2 + 3 + 4)
        err(2, "循环里累加的结果不对");
    __c6_co_destroy(t);
}

test "defer 跨挂起仍然按逆序执行" {
    struct with_defer_task *t = NULL;
    lg_reset();
    t = with_defer();
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 5)
        err(2, "defer 之后的返回值不对");
    if (!LG_IS("bodyd2d1"))
        err(3, "defer 的顺序不对（defer 记录没有跟着帧一起恢复）");
    __c6_co_destroy(t);
}

test "await void 的 async 函数" {
    struct fire_user_task *t = NULL;
    lg_reset();
    t = fire_user();
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 1 || !LG_IS("f"))
        err(2, "await void 的 async 函数没有等到它执行");
    __c6_co_destroy(t);
}

test "await c6_io 真的挂起再恢复" {
    struct io_rounds_task *t = io_rounds();
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 3)
        err(2, "c6_io 的循环次数不对");
    __c6_co_destroy(t);
}

test "任务槽会被回收，超过池容量也能跑" {
    int i = 0;
    for (i = 0; i < 200; i++) {
        struct add_task *t = add(i, 1);
        if (__c6_co_wait(t) != 0)
            err(1, "驱动失败");
        if (t->val != i + 1)
            err(2, "任务槽回收后的返回值不对");
        __c6_co_destroy(t);
    }
}

/* ---- task / go ---- */

static int pipe_a[2] = {0, 0};
static int pipe_b[2] = {0, 0};

/* 每个任务等自己的 fd 就绪，跑的顺序能从日志里看出来 */
async int on_fd(int fd, int id)
{
    char buf[2] = {0};
    int rev = 0;
    lgc((char)('a' + id));
    rev = await c6_io(fd, C6_CO_EV_READ);
    if (rev > 0)
        read(fd, buf, 1);
    lgc((char)('A' + id));
    return rev;
}

async int next_id(int i)
{
    return await add(i, 1);
}

/* go 写在 async 体里也必须是顶层任务：handler 挂起时它照样往前跑 */
async int wait_own(void)
{
    char buf[2] = {0};
    int rev = 0;
    lgc('g');
    rev = await c6_io(pipe_b[0], C6_CO_EV_READ);
    if (rev > 0)
        read(pipe_b[0], buf, 1);
    lgc('G');
    return rev;
}

async int wait_other(void)
{
    char buf[2] = {0};
    int rev = await c6_io(pipe_a[0], C6_CO_EV_READ);
    if (rev > 0)
        read(pipe_a[0], buf, 1);
    return rev;
}

async int spawner(void)
{
    go wait_own();
    lgc('h');
    await wait_other();
    lgc('H');
    return 3;
}

async int!error failing(void)
{
    err(9, "failing task");
}

test "task 声明的句柄能拿到返回值" {
    task t = add(20, 22);
    if (__c6_co_wait(t) != 0)
        err(1, "驱动失败");
    if (t->val != 42)
        err(2, "task 句柄的值不对");
}

test "一行多个 task 声明" {
    task a = add(1, 2), b = add(3, 4);
    if (__c6_co_wait(a) != 0 || __c6_co_wait(b) != 0)
        err(1, "驱动失败");
    if (a->val != 3 || b->val != 7)
        err(2, "多个 task 声明的值不对");
}

test "task 句柄在作用域退出时自动回收（超过池容量）" {
    int i = 0;
    for (i = 0; i < 200; i++) {
        task h = next_id(i);
        if (__c6_co_wait(h) != 0)
            err(1, "驱动失败");
        if (h->val != i + 1)
            err(2, "自动回收后的返回值不对");
    }
}

test "go 立刻跑到第一个 await 点" {
    int fds[2] = {0, 0};
    char one = 'x';
    char buf[2] = {0};
    if (pipe(fds))
        err(1, "pipe 失败");
    write(fds[1], &one, 1);
    lg_reset();
    go on_fd(fds[0], 0);        /* 日志里应立刻出现 'a' */
    if (!LG_IS("a"))
        err(2, "go 没有在语句处先跑到第一个 await");
    __c6_co_run();              /* 这里才继续跑完 */
    if (!LG_IS("aA"))
        err(3, "go 的任务没有被调度器推进完");
    (void)buf;
}

test "多个 go 会被轮转推进（真交替，不是顺序跑完）" {
    char one = 'x';
    if (pipe(pipe_a) || pipe(pipe_b))
        err(1, "pipe 失败");
    write(pipe_a[1], &one, 1);
    write(pipe_b[1], &one, 1);
    lg_reset();
    go on_fd(pipe_a[0], 0);
    go on_fd(pipe_b[0], 1);
    /* 两个都停在第一个 await 上 */
    if (!LG_IS("ab"))
        err(2, "两个 go 的首步顺序不对");
    __c6_co_run();
    /* 顺序是后进先出：b 先被推进一步，然后 a —— 关键是两者交叉（abBA），
       而不是把某一个直接跑完（那会是 abBB… 之类） */
    if (!LG_IS("abBA"))
        err(3, "两个 go 不是交替推进的");
}

test "go 写在 async 体里也是顶层任务" {
    char one = 'x';
    if (pipe(pipe_a) || pipe(pipe_b))
        err(1, "pipe 失败");
    write(pipe_a[1], &one, 1);
    write(pipe_b[1], &one, 1);
    lg_reset();
    {
        task h = spawner();
        if (__c6_co_wait(h) != 0)
            err(1, "驱动失败");
        if (h->val != 3)
            err(2, "spawner 的返回值不对");
    }
    /* g: go 的首步；h: 继续往下；G: handler 挂起期间 go 出去的顶层任务
       被推进；H: handler 恢复 */
    if (!LG_IS("ghGH"))
        err(3, "go 出去的协程不是顶层的（被外层挂起冻结了）");
}

test "go 不产生值，但 __c6_co_run 会报告失败的任务数" {
    int i = 0;
    for (i = 0; i < 40; i++)         /* 每个都在首步完成并回收 */
        go next_id(i);
    if (__c6_co_run() != 0)
        err(1, "成功的 go 不该被计为失败");
    go failing();
    go failing();
    if (__c6_co_run() != 2)
        err(2, "__c6_co_run 没有报告失败的 go 任务");
}
