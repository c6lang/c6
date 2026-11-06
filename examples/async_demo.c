/* async / await 用法演示：一个 HTTP 请求
 *
 * 编译运行（在仓库根目录）：
 *   make build
 *   ./tcc/tcc -B tcc -std=c6 -run examples/async_demo.c
 *
 * 真实客户端里 connect / send / recv 都会阻塞。这里把"等 fd 就绪"抽成
 * await c6_io(fd, ev)，于是三层 async 函数可以像同步代码一样顺着写：
 *
 *     int fd = await connect_to(...);
 *     await send_all(fd, req, n);
 *     int status = await recv_status(fd, buf, sizeof buf);
 *
 * 为了让示例离线可跑，传输层用 pipe() 代替 socket，服务端由本文件里的
 * respond() 扮演；换成 socket + connect() 时异步结构完全不变。
 *
 * 后半段演示 co / task 两个关键字：
 *   task t = http_get(...);   句柄留在手上，作用域退出自动 destroy
 *   co worker(...);           顶层任务，交给 __c6_co_run() 跑完
 */
#include <stdio.h>
#include <string.h>

/* 只用到这几个 libc 函数 */
int pipe(int *fds);
int read(int fd, void *buf, int len);
int write(int fd, const void *buf, int len);
int close(int fd);

/* ---- 传输层：三个 async 原语 ---- */

/* 等 fd 可写就当作"连上了"；真实实现里这里是 connect() + 非阻塞等待 */
async int connect_to(int wfd)
{
    int rev = 0;
    rev = await c6_io(wfd, C6_CO_EV_WRITE);
    if (rev <= 0)
        return -1;
    return wfd;
}

/* 循环写完整块：每次可写才写，写完再等下一次 */
async int send_all(int wfd, const char *data, int len)
{
    int off = 0;
    int n = 0;
    while (off < len) {
        await c6_io(wfd, C6_CO_EV_WRITE);
        n = write(wfd, data + off, len - off);
        if (n <= 0)
            return -1;
        off += n;
    }
    return off;
}

/* 读到空行为止的响应头，顺手把状态码解出来 */
async int recv_status(int rfd, char *buf, int cap)
{
    int len = 0;
    int n = 0;
    int status = -1;
    while (len < cap - 1) {
        await c6_io(rfd, C6_CO_EV_READ);
        n = read(rfd, buf + len, cap - 1 - len);
        if (n <= 0)
            break;
        len += n;
        buf[len] = '\0';
        if (len >= 12 && buf[8] != '\0') {
            /* "HTTP/1.1 200 OK" */
            status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10
                     + (buf[11] - '0');
            break;
        }
    }
    return status;
}

/* ---- 业务层：一次 GET 就是三行 await ---- */

async int http_get(int wfd, int rfd, const char *req, char *buf, int cap)
{
    int fd = 0;
    int sent = 0;
    int status = 0;

    fd = await connect_to(wfd);
    if (fd < 0)
        return -1;

    sent = await send_all(fd, req, (int)strlen(req));
    if (sent < 0)
        return -2;

    status = await recv_status(rfd, buf, cap);
    return status;
}

/* ---- 模拟服务端：往 pipe 里塞一段固定响应 ---- */

static void respond(int wfd)
{
    static const char resp[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "hi";
    write(wfd, resp, sizeof resp - 1);
    close(wfd);
}

/* ---- co / task：两个协程互相唤醒 ---- */

/* 等自己的 fd 就绪，然后往下一个 fd 写一个字节，把下一个协程叫醒。
   next_wfd < 0 表示"我是最后一个"。 */
async int stage(int rfd, int next_wfd, int id)
{
    char b[2] = {0};
    int rev = 0;

    printf("  stage%d: 等 fd %d\n", id, rfd);
    rev = await c6_io(rfd, C6_CO_EV_READ);
    if (rev > 0) {
        read(rfd, b, 1);
        printf("  stage%d: 醒了\n", id);
    }
    if (next_wfd >= 0) {
        write(next_wfd, "x", 1);
        printf("  stage%d: 写 fd %d，叫醒下一个\n", id, next_wfd);
    }
    return rev;
}

int main(void)
{
    int up[2] = {0, 0};       /* 客户端 -> 服务端 */
    int down[2] = {0, 0};     /* 服务端 -> 客户端 */
    int c1[2] = {0, 0};       /* co 演示用的两个管道 */
    int c2[2] = {0, 0};
    char buf[256] = {0};
    int rc = 0;

    if (pipe(up) || pipe(down)) {
        printf("pipe() failed\n");
        return 1;
    }
    respond(down[1]);         /* 服务端先准备好响应 */
    close(down[1]);

    /* task：句柄留在手上，走到作用域末尾自动 __c6_co_destroy */
    {
        task t = http_get(up[1], down[0],
                          "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n",
                          buf, sizeof buf);

        rc = __c6_co_wait(t);     /* 驱动协程直到完成 */
        printf("wait rc=%d status=%d\n", rc, t->val);
        printf("response:\n%s\n", buf);
    }
    close(up[1]);
    close(down[0]);

    /* co：先起一个会 park 在 c2 上的，再起一个把 c2 叫醒的 */
    if (pipe(c1) || pipe(c2))
        return 1;
    write(c1[1], "x", 1);        /* 只让 c1 先就绪 */
    printf("co stage2：c2 还没数据，它在 co 语句里 park 住\n");
    co stage(c2[0], -1, 2);
    printf("co stage1：c1 已就绪，但 await c6_io 也要等一轮 select\n");
    co stage(c1[0], c2[1], 1);
    printf("回到 main：两个 co 都返回了，两个任务都还 park 着\n");
    printf("__c6_co_run：唤醒 stage1，它写 c2 再唤醒 stage2；errs=%d\n",
           __c6_co_run());
    close(c1[1]);
    close(c2[1]);

    return rc == 0 ? 0 : 1;
}
