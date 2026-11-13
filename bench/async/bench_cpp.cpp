// 异步编程基准：C++（C++20 协程 + epoll）
//
// 和 c6 的 async/await 是同一种机制：无栈协程 + 单线程事件循环。指标与
// bench.c6 / bench_c.c / bench_go.go / bench_rs.rs 对齐：
//
//   create  建 200000 个协程并跑完，报每个多少纳秒
//   switch  一次「挂起在 fd 上 → 内核就绪 → 恢复」的往返，跑 50000 轮
//   conns   1000 路并发连接，事件循环一次把 1000 路服务完，报每路多少微秒
//           + 每个挂起协程的 RSS 增量
//
// 编译：g++ -std=c++20 -O2 -o build/bench_cpp bench/async/bench_cpp.cpp
#include <cerrno>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/epoll.h>

using std::coroutine_handle;
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

constexpr int CREATE_N = 200000;
constexpr int SWITCH_N = 50000;
constexpr int CONN_N = 1000;

static long long now_ns()
{
    timespec ts{};

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static long rss_kb()
{
    FILE *f = fopen("/proc/self/statm", "r");
    long total = 0, resident = 0;

    if (!f)
        return -1;
    if (fscanf(f, "%ld %ld", &total, &resident) != 2)
        resident = -1;
    fclose(f);
    return resident < 0 ? -1 : resident * 4;
}

static void raise_fds()
{
    rlimit rl{};

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
        return;
    if (rl.rlim_cur >= 4096)
        return;
    rl.rlim_cur = rl.rlim_max < 4096 ? rl.rlim_max : 4096;
    setrlimit(RLIMIT_NOFILE, &rl);
}

/* ---- 一个最小的 Task：initial_suspend 挂住，由执行器 resume ---- */

struct Task {
    struct promise_type {
        Task get_return_object()
        {
            return Task{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::abort(); }
    };
    std::coroutine_handle<promise_type> h{};
};

/* ---- 等 fd 可读的 awaiter：登记到 epoll（EPOLLONESHOT），就绪后恢复 ---- */

static int g_ep = -1;
static std::unordered_map<int, std::coroutine_handle<>> g_waiting;
static std::unordered_map<int, unsigned int> g_armed;   /* fd -> 内核已知事件 */
static int g_ready_fd = -1;

struct WaitRead {
    int fd;
    int rev = 0;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        epoll_event e{};

        e.events = EPOLLIN | EPOLLONESHOT;
        e.data.fd = fd;
        auto it = g_armed.find(fd);

        if (it == g_armed.end()) {
            if (epoll_ctl(g_ep, EPOLL_CTL_ADD, fd, &e) != 0) {
                g_waiting[fd] = h;
                return;
            }
        } else if (epoll_ctl(g_ep, EPOLL_CTL_MOD, fd, &e) != 0) {
            epoll_ctl(g_ep, EPOLL_CTL_ADD, fd, &e);
        }
        g_armed[fd] = EPOLLIN;
        g_waiting[fd] = h;
    }

    int await_resume() const noexcept { return rev; }
};

/* 一次 epoll_wait，把就绪的协程就地恢复掉 */
static void wait_one()
{
    epoll_event ev{};
    int n = epoll_wait(g_ep, &ev, 1, -1);

    if (n < 1)
        return;
    {
        auto it = g_waiting.find(ev.data.fd);

        if (it == g_waiting.end())
            return;
        auto h = it->second;

        g_waiting.erase(it);
        h.resume();
    }
}

static Task trivial(int id, int *out)
{
    *out = id + 1;
    co_return;
}

struct ReadOne {
    int fd;
    int n = 0;
    char buf[1] = { 0 };
};

static Task read_one(ReadOne *st)
{
    WaitRead w{ st->fd };

    st->n = co_await w;
    if (st->n > 0)
        read(st->fd, st->buf, 1);
    co_return;
}

static void bench_create()
{
    long long t0 = now_ns();
    int acc = 0;
    int out = 0;

    for (int i = 0; i < CREATE_N; i++) {
        Task t = trivial(i, &out);
        t.h.resume();
        acc += out;
        t.h.destroy();
    }
    printf("create_ns_per_task,%.1f\n", (double)(now_ns() - t0) / CREATE_N);
    if (acc == 0)
        printf("create_note,acc 0\n");
}

static void bench_switch()
{
    int fds[2];

    if (pipe(fds) != 0)
        return;
    g_ep = epoll_create1(0);
    if (g_ep < 0)
        return;
    ReadOne st{ fds[0] };
    long long t0 = now_ns();
    for (int i = 0; i < SWITCH_N; i++) {
        Task t = read_one(&st);
        t.h.resume();                     /* 跑到挂起点 */
        char c = 'x';
        write(fds[1], &c, 1);
        wait_one();                       /* 就绪 → 就地恢复 */
        st.n = 0;
        if (t.h && !t.h.done())
            t.h.destroy();
    }
    printf("switch_ns_per_round,%.1f\n", (double)(now_ns() - t0) / SWITCH_N);
    close(g_ep);
    close(fds[0]);
    close(fds[1]);
}

static void bench_conns()
{
    std::vector<std::pair<int, int>> fds(CONN_N);
    std::vector<ReadOne> st(CONN_N);
    std::vector<Task> tasks;
    long before = 0, after = 0;
    long long t0 = 0;

    tasks.reserve(CONN_N);
    for (int i = 0; i < CONN_N; i++) {
        int p[2];

        if (pipe(p) != 0) {
            printf("conns_error,pipe %d\n", i);
            return;
        }
        fds[i] = { p[0], p[1] };
        st[i].fd = p[0];
    }
    g_ep = epoll_create1(0);
    if (g_ep < 0)
        return;
    before = rss_kb();
    for (int i = 0; i < CONN_N; i++) {
        tasks.push_back(read_one(&st[i]));
        tasks.back().h.resume();           /* 全部挂到 epoll 上 */
    }
    after = rss_kb();
    t0 = now_ns();
    for (int i = 0; i < CONN_N; i++) {
        char c = 'x';

        write(fds[i].second, &c, 1);
    }
    {
        epoll_event evs[CONN_N];
        int n = epoll_wait(g_ep, evs, CONN_N, -1);

        for (int i = 0; i < n; i++) {
            auto it = g_waiting.find(evs[i].data.fd);

            if (it == g_waiting.end())
                continue;
            auto h = it->second;
            g_waiting.erase(it);
            h.resume();
        }
        if (n != CONN_N)
            printf("conns_note,ready %d/%d\n", n, CONN_N);
    }
    {
        long long dt = now_ns() - t0;

        printf("conns_us_per_conn,%.3f\n", (double)dt / 1000.0 / CONN_N);
    }
    printf("rss_bytes_per_task,%.1f\n",
           before > 0 && after > 0 ? (double)(after - before) * 1024.0 / CONN_N : -1.0);
    for (auto &t : tasks)
        if (t.h)
            t.h.destroy();
    close(g_ep);
}

int main()
{
    const char *sec = getenv("BENCH_SEC");

    printf("lang,c++(C++20 协程+epoll)\n");
    raise_fds();
    if (!sec || !*sec || !strcmp(sec, "all") || !strcmp(sec, "create"))
        bench_create();
    if (!sec || !*sec || !strcmp(sec, "all") || !strcmp(sec, "switch"))
        bench_switch();
    if (!sec || !*sec || !strcmp(sec, "all") || !strcmp(sec, "conns"))
        bench_conns();
    return 0;
}
