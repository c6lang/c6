/* 异步编程基准：C
 *
 * 两种机制，`./bench_c epoll`（默认）与 `./bench_c threads`：
 *   epoll    单线程事件循环 + 手写状态机（和 c6 的 async/await 同构）
 *   threads  一任务一线程（pthread，C 里最常用的「并发」写法）
 *
 * 指标与 bench.c6 / bench_cpp.cpp / bench_go.go / bench_rs.rs 完全对齐：
 *   create  建 200000 个任务（create + join），报每个任务多少纳秒
 *   switch  同一线程上「挂起 → 内核就绪 → 恢复」跑 50000 轮，报每轮多少纳秒
 *   conns   1000 路并发连接，事件循环一次把 1000 路服务完，报每路多少微秒，
 *           同时报每个挂起任务的 RSS 增量（字节）
 *
 * 编译：gcc -O2 -o build/bench_c bench/async/bench_c.c -lpthread
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define CREATE_N 200000
#define SWITCH_N 50000
#define CONN_N 1000

static int use_threads = 0;

static long long now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static long rss_kb(void)
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

static void raise_fds(void)
{
    struct rlimit rl;

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
        return;
    if (rl.rlim_cur >= 4096)
        return;
    rl.rlim_cur = rl.rlim_max < 4096 ? rl.rlim_max : 4096;
    setrlimit(RLIMIT_NOFILE, &rl);
}

/* ---- 一个最便宜的任务 ---- */

static void *trivial(void *arg)
{
    return (char *)arg + 1;
}

static void bench_create(void)
{
    long long t0 = now_ns();
    int i = 0;

    if (use_threads) {
        for (i = 0; i < CREATE_N; i++) {
            pthread_t th;

            if (pthread_create(&th, NULL, trivial, NULL) != 0)
                return;
            pthread_join(th, NULL);
        }
    } else {
        for (i = 0; i < CREATE_N; i++) {
            /* 状态机版最便宜的「任务」：一次 malloc + 一次函数调用 + 一次 free */
            void *t = malloc(sizeof(void *));

            if (!t)
                return;
            trivial(t);
            free(t);
        }
    }
    printf("create_ns_per_task,%.1f\n", (double)(now_ns() - t0) / CREATE_N);
}

/* ---- 一次真实的「挂起 → 就绪 → 恢复」 ---- */


static int g_a[2], g_b[2];

static void *pong_thread(void *arg)
{
    int i = 0;

    (void)arg;
    for (i = 0; i < SWITCH_N; i++) {
        char c;

        if (read(g_a[0], &c, 1) != 1)
            break;
        if (write(g_b[1], "x", 1) != 1)
            break;
    }
    return NULL;
}

static void bench_switch(void)
{
    int fds[2] = { -1, -1 };
    long long t0 = 0;
    int i = 0;

    if (use_threads) {
        pthread_t th;
        char c;

        if (pipe(g_a) != 0 || pipe(g_b) != 0)
            return;
        if (pthread_create(&th, NULL, pong_thread, NULL) != 0)
            return;
        t0 = now_ns();
        for (i = 0; i < SWITCH_N; i++) {
            if (write(g_a[1], "x", 1) != 1)
                return;
            if (read(g_b[0], &c, 1) != 1)
                return;
        }
        printf("switch_ns_per_round,%.1f\n", (double)(now_ns() - t0) / SWITCH_N);
        pthread_join(th, NULL);
        return;
    }
    if (pipe(fds) != 0)
        return;
    {
        int ep = epoll_create1(0);
        struct epoll_event ev;

        if (ep < 0)
            return;
        memset(&ev, 0, sizeof ev);
        ev.events = EPOLLIN;
        ev.data.fd = fds[0];
        if (epoll_ctl(ep, EPOLL_CTL_ADD, fds[0], &ev) != 0)
            return;
        t0 = now_ns();
        for (i = 0; i < SWITCH_N; i++) {
            char c;

            if (write(fds[1], "x", 1) != 1)
                return;
            if (epoll_wait(ep, &ev, 1, -1) < 1)
                return;
            if (read(fds[0], &c, 1) != 1)
                return;
        }
        printf("switch_ns_per_round,%.1f\n", (double)(now_ns() - t0) / SWITCH_N);
        close(ep);
    }
    close(fds[0]);
    close(fds[1]);
}

/* ---- 1000 路并发连接 ---- */

struct Conn {
    int rfd, wfd;
    char buf[1];
};

static void *conn_reader(void *arg)
{
    struct Conn *c = (struct Conn *)arg;
    char b;

    if (read(c->rfd, &b, 1) != 1)
        return NULL;
    return NULL;
}

static void bench_conns(void)
{
    static struct Conn conns[CONN_N];
    static pthread_t th[CONN_N];
    long before = 0, after = 0;
    long long t0 = 0, dt = 0;
    int i = 0;

    for (i = 0; i < CONN_N; i++) {
        int fds[2];

        if (pipe(fds) != 0) {
            printf("conns_error,pipe %d\n", i);
            return;
        }
        conns[i].rfd = fds[0];
        conns[i].wfd = fds[1];
    }
    before = rss_kb();
    if (use_threads) {
        for (i = 0; i < CONN_N; i++)
            if (pthread_create(&th[i], NULL, conn_reader, &conns[i]) != 0) {
                printf("conns_error,thread %d\n", i);
                return;
            }
    }
    after = rss_kb();
    /* 每路一个字节等着；事件循环（或线程调度）一次把 1000 路收完。
       协程版本的「挂起 / 登记」都在计时窗口之外，所以这里的 epoll 登记也
       放在 t0 之前：量的是同一件事——就绪之后把 1000 路收完要多久。 */
    if (use_threads) {
        t0 = now_ns();
        for (i = 0; i < CONN_N; i++)
            write(conns[i].wfd, "x", 1);
        for (i = 0; i < CONN_N; i++)
            pthread_join(th[i], NULL);
        dt = now_ns() - t0;
        printf("conns_us_per_conn,%.3f\n", (double)dt / 1000.0 / CONN_N);
    } else {
        int ep = epoll_create1(0);
        struct epoll_event *evs = calloc(CONN_N, sizeof *evs);
        int n = 0;

        if (ep < 0 || !evs)
            return;
        for (i = 0; i < CONN_N; i++) {
            struct epoll_event ev;

            memset(&ev, 0, sizeof ev);
            ev.events = EPOLLIN;
            ev.data.fd = conns[i].rfd;
            if (epoll_ctl(ep, EPOLL_CTL_ADD, conns[i].rfd, &ev) != 0) {
                printf("conns_error,ctl %d\n", i);
                return;
            }
        }
        t0 = now_ns();
        for (i = 0; i < CONN_N; i++)
            write(conns[i].wfd, "x", 1);
        n = epoll_wait(ep, evs, CONN_N, -1);
        for (i = 0; i < n; i++) {
            char b;

            if (read(evs[i].data.fd, &b, 1) != 1)
                break;
        }
        dt = now_ns() - t0;
        close(ep);
        free(evs);
        if (n != CONN_N)
            printf("conns_note,ready %d/%d\n", n, CONN_N);
        printf("conns_us_per_conn,%.3f\n", (double)dt / 1000.0 / CONN_N);
    }
    printf("rss_bytes_per_task,%.1f\n",
           before > 0 && after > 0 ? (double)(after - before) * 1024.0 / CONN_N : -1.0);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "threads") == 0)
        use_threads = 1;
    /* BENCH_SEC 选跑哪一段：create / switch / conns（默认全跑），
       每个指标单独一个进程，RSS 增量才是干净的 */
    {
        const char *sec = getenv("BENCH_SEC");

        printf("lang,c(%s)\n", use_threads ? "pthread" : "epoll+状态机");
        raise_fds();
        if (!sec || !*sec || strcmp(sec, "all") == 0 || strcmp(sec, "create") == 0)
            bench_create();
        if (!sec || !*sec || strcmp(sec, "all") == 0 || strcmp(sec, "switch") == 0)
            bench_switch();
        if (!sec || !*sec || strcmp(sec, "all") == 0 || strcmp(sec, "conns") == 0)
            bench_conns();
    }
    return 0;
}
