/* c6 自测：async 的帧住在任务里（阶段 P7，A3 "frame in task"）
 *
 * 运行（仓库根目录）：
 *   make selftest
 * 或单个文件：
 *   ./tcc/tcc -B tcc -std=c6 -test tests/async_frame.c
 *
 * 每个后端都回答一个问题（c6_backend_frame_in_task()）：async body 能不能
 * 直接在任务自己那块内存上跑？能的话，帧基址来自运行期传进来的指针，挂起
 * 不再拷贝任何东西，帧也不会被搬到别的地址 —— 于是：
 *   - '&local' 跨 await 一直有效（下面第 1 组），
 *   - 局部数组不再受 4 KiB 上限约束（第 2 组），
 *   - 'weak'（指向槽本身的指针）在 async 里可以使用（第 3 组），
 *   - 任务数不再受固定池大小限制（第 4 组），
 *   - defer 体在恢复之后写的还是同一批局部（第 5 组），
 *   - 块作用域的 defer 在恢复之后退出块时照样执行（第 8 组）。
 */
/* 只用到这几个 libc 函数（不给 libtcc1 添依赖，也不 include 头文件，
   这样交叉目标至少能编译这个文件） */
int pipe(int *fds);
int read(int fd, void *buf, int len);
int write(int fd, const void *buf, int len);

/* ---- 1. 地址稳定：co 起始、__c6_co_run() 恢复 ---- */
static int g_addr_ok = 0;

async void addr_body(int fd)
{
    int x = 1;
    int *p = &x;
    int *before = &x;
    int rev = 0;

    *p = 2;
    rev = await c6_io(fd, 2);          /* 可写：立即就绪 */
    (void)rev;
    /* 挂起之前取的地址在恢复之后必须仍然指向同一个 x */
    g_addr_ok = (&x == before) && (*p == 2) && (p == &x);
    x = 3;
    g_addr_ok = g_addr_ok && (*p == 3);
}

test "&local 跨 await 保持不变" {
    int fds[2] = {-1, -1};

    if (pipe(fds) != 0)
        err(1, "pipe 失败");
    g_addr_ok = 0;
    co addr_body(fds[1]);
    if (__c6_co_run() < 0)
        err(2, "__c6_co_run 失败");
    if (!g_addr_ok)
        err(3, "恢复后 '&local' 指向了别处（帧被搬走了）");
}

/* ---- 2. 帧大小不再有 4 KiB 上限 ---- */
static int g_big = 0;

async void big_body(int fd)
{
    char buf[8192] = {0};

    buf[0] = 1;
    buf[8000] = 7;
    buf[8191] = 9;
    await c6_io(fd, 2);
    g_big = buf[0] + buf[8000] + buf[8191];    /* 1 + 7 + 9 */
}

test "8 KiB 局部数组挂起后仍在" {
    int fds[2] = {-1, -1};

    if (pipe(fds) != 0)
        err(10, "pipe 失败");
    g_big = 0;
    co big_body(fds[1]);
    if (__c6_co_run() < 0)
        err(11, "__c6_co_run 失败");
    if (g_big != 17)
        err(12, "8 KiB 的局部数组在挂起/恢复中丢了内容");
}

/* ---- 3. weak 在 async 里合法：局部、形参、返回值 ---- */
struct Node { int v; };

async weak @Node weak_make(void)
{
    return NULL;
}

async int weak_body(int fd, weak @Node param)
{
    weak @Node wl = NULL;
    @Node keep = new Node;
    int a = 0, b = 0, c = 0, d = 0;

    keep->v = 5;
    wl = keep;                         /* 弱引用：不计数 */
    /* 弱引用不能直接解引用，要先升级成计数引用（升级只活在这个块里） */
    {
        @Node q = wl;
        a = q->v;                      /* 5 */
        q = NULL;
    }
    d = (param == NULL) ? 1 : 0;       /* 形参传的是 NULL */
    await c6_io(fd, 2);
    {
        @Node q = wl;
        b = q->v;                      /* keep 还活着：还是 5 */
        q = NULL;
    }
    keep = NULL;                       /* 对象在这里死掉：弱引用读成 NULL */
    c = (wl == NULL) ? 1 : 0;
    return a * 1000 + b * 100 + c * 10 + d;     /* 5000 + 500 + 10 + 1 */
}

test "weak 局部 / 形参 / 返回值在 async 里可用" {
    int fds[2] = {-1, -1};
    struct weak_body_task *t = NULL;
    weak @Node w = NULL;

    if (pipe(fds) != 0)
        err(20, "pipe 失败");
    t = weak_body(fds[1], w);
    if (__c6_co_wait(t) != 0)
        err(21, "__c6_co_wait 失败");
    if (t->val != 5511)
        err(22, "async 里 weak 局部读到的值不对");
    __c6_co_destroy(t);
    {
        struct weak_make_task *m = weak_make();
        if (__c6_co_wait(m) != 0)
            err(23, "返回 weak 的任务失败");
        if (m->val != NULL)
            err(24, "weak 返回值不该非空");
        __c6_co_destroy(m);
    }
}

/* ---- 4. 任务数不再受固定池限制 ---- */
static int g_many = 0;

async void many_body(int fd)
{
    int rev = await c6_io(fd, 2);

    (void)rev;
    g_many++;
}

test "同时挂起 200 个任务" {
    int fds[2] = {-1, -1};
    int i = 0;

    if (pipe(fds) != 0)
        err(30, "pipe 失败");
    g_many = 0;
    for (i = 0; i < 200; i++)
        co many_body(fds[1]);
    if (__c6_co_run() < 0)
        err(31, "__c6_co_run 失败");
    if (g_many != 200)
        err(32, "同时挂起的任务数被上限截断了");
}

/* ---- 5. defer 体在恢复之后写的还是同一批局部 ---- */
static int g_defer_x = 0;

async int defer_after(int fd)
{
    int x = 1;

    defer {
        g_defer_x = x + 10;            /* 读恢复之后的 x */
    }
    await c6_io(fd, 2);
    x += 100;
    return x;                          /* 101 */
}

test "defer 在恢复之后写到同一批局部" {
    int fds[2] = {-1, -1};
    struct defer_after_task *t = NULL;

    if (pipe(fds) != 0)
        err(40, "pipe 失败");
    g_defer_x = 0;
    t = defer_after(fds[1]);
    if (__c6_co_wait(t) != 0)
        err(41, "__c6_co_wait 失败");
    if (t->val != 101 || g_defer_x != 111)
        err(42, "defer 体在恢复之后读到的不是同一批局部");
    __c6_co_destroy(t);
}

/* ---- 6. 参数从任务前缀拷进任务帧，恢复后还是它 ---- */
async int param_body(int fd, int a, int b)
{
    int s = a + b;

    await c6_io(fd, 2);
    return s * 10 + a + b;             /* (a+b)*10 + a + b */
}

test "形参在帧里活过挂起" {
    int fds[2] = {-1, -1};
    struct param_body_task *t = NULL;

    if (pipe(fds) != 0)
        err(50, "pipe 失败");
    t = param_body(fds[1], 3, 4);
    if (__c6_co_wait(t) != 0)
        err(51, "__c6_co_wait 失败");
    if (t->val != 77)
        err(52, "形参挂起后丢了");
    __c6_co_destroy(t);
}

/* ---- 7. 挂起的任务被销毁：回卷跑 defer、归还引用 ---- */
/* 回卷按错误路径派发（`__c6_co_kind(task) == ABORT` 时发的是 mode 2），所以
   `errdefer` 也会跑：被销毁的任务没有"正常结束"这回事。 */
static int g_abort_clean = 0;
static int g_abort_errdef = 0;

async int abandoned(int fd)
{
    char pad[2048] = {0};
    @Node n = new Node;

    n->v = 9;
    pad[2040] = 5;
    defer {
        g_abort_clean += n->v + pad[2040];   /* 14 = 9 + 5 */
    }
    errdefer g_abort_errdef += 1;
    await c6_io(fd, 1);              /* 读端没有数据：挂在这里 */
    return n->v + pad[2040];
}

test "挂起的任务被销毁时回卷" {
    int fds[2] = {-1, -1};
    int live0 = 0;

    if (pipe(fds) != 0)
        err(60, "pipe 失败");
    live0 = (int)__c6_gc_live();
    g_abort_clean = 0;
    g_abort_errdef = 0;
    {
        task t = abandoned(fds[0]);

        /* task t = f(...) 只创建任务；start() 才把它跑到第一个挂起点 */
        if (t->start() == C6_CO_KIND_DONE)
            err(61, "任务应当挂在 fd 上，而不是已经结束");
        if (t->done())
            err(62, "挂起的任务不该报 done");
        if (g_abort_clean != 0)
            err(63, "defer 在挂起时不该运行");
        if (g_abort_errdef != 0)
            err(67, "errdefer 在挂起时不该运行");
        if ((int)__c6_gc_live() <= live0)
            err(64, "任务持有的引用没有计数");
    }                                /* 作用域退出 -> __c6_co_destroy -> 回卷 */
    if (g_abort_clean != 14)
        err(65, "回卷没有跑 defer，或帧里的局部量不对");
    if (g_abort_errdef != 1)
        err(68, "回卷按错误路径派发，errdefer 也应该跑一次");
    if ((int)__c6_gc_live() != live0)
        err(66, "回卷没有把托管引用还回来");
}

/* ---- 8. 块作用域的 defer 跨过 await ---- */
/* 注册发生在挂起之前，块的退出发生在恢复之后：位图在任务帧里，所以这次
   清理请求一样能找到它。 */
static int g_scope_log = 0;

async int scope_body(int fd)
{
    int x = 1;

    {
        defer {
            g_scope_log = g_scope_log * 10 + x;   /* 读块退出时的 x */
        }
        x = 2;
        await c6_io(fd, 2);            /* 挂起期间这个位留在任务的帧里 */
        x = 3;
    }                                  /* 恢复之后才退出这个块 */
    return x + 100;
}

test "块作用域的 defer 越过 await" {
    int fds[2] = {-1, -1};
    struct scope_body_task *t = NULL;

    if (pipe(fds) != 0)
        err(70, "pipe 失败");
    g_scope_log = 0;
    t = scope_body(fds[1]);
    if (__c6_co_wait(t) != 0)
        err(71, "__c6_co_wait 失败");
    if (t->val != 103)
        err(72, "返回值不对");
    if (g_scope_log != 3)
        err(73, "恢复之后块退出没有跑 defer，或读到的局部量不对");
    __c6_co_destroy(t);
}
