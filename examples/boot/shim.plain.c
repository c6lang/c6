/* 裸机上的 libc 替身：只为了让 tests/*.c 原样跑起来。文件名带 .plain 是为了
 * 让构建脚本按**普通 C**编它（c6 模式要求每个变量都有初始化器，而这里的 4MB
 * 堆必须落在 .bss 里，不能写 `= {0}` 把它搬进 .data）。
 *
 *   cmake --build build --target fstest-bare-tests
 *
 * 没有内核，所以这里没有 syscall：输出是 qemu 的 0xE9 调试口，管道是内存里的
 * 一个环，select 就是"现在有没有数据/有没有空位"，stat/getpid/usleep 是骗人的
 * 桩子。写这些不是为了给运行时补依赖（libtcc1 的依赖全是 weak，不提供也能链接
 * 和运行），而是因为**测试文件本身**是用 libc 写的。
 *
 * 不支持 %f/%e/%g（tests 里没有一处真的用它打印浮点，只有注释里提到）和
 * 宽字符。qsort 是插入排序，元素的临时缓冲固定 128 字节。
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* 这几个不走系统头文件：<unistd.h>/<sys/stat.h> 的声明带 size_t / struct stat，
   而裸机上没有那套类型，这里按寄存器 ABI 兼容的方式自己声明（测试文件里自己
   会包含真头文件，它们的调用约定与这里一致）。 */
int pipe(int *fds);
int read(int fd, void *buf, int len);
int write(int fd, const void *buf, int len);
int select(int nfds, void *rd, void *wr, void *ex, void *tv);
int getpid(void);
int usleep(unsigned int us);
int stat(const char *path, void *buf);
void abort(void);

void boot_putc(int ch);                 /* examples/boot/io.c */
void boot_exit(int code);

static void shim_out(const char *s, int n)
{
    int i = 0;

    for (i = 0; i < n; i++)
        boot_putc((unsigned char)s[i]);
}

static void shim_puts_(const char *s)
{
    while (*s)
        boot_putc((unsigned char)*s++);
}

/* ---- printf（整数/字符串/字符/指针，带 flag、宽度、精度） ---- */

static int fmt_digits(char *b, unsigned long long v, int base, int upper)
{
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char t[24] = {0};
    int n = 0, i = 0;

    if (!v)
        t[n++] = '0';
    while (v) {
        t[n++] = dig[v % (unsigned)base];
        v /= (unsigned)base;
    }
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    return n;
}

static void fmt_pad(const char *body, int n, int width, int ljust, int zero,
                    const char *prefix, int pn)
{
    int pad = width - n - pn;
    int i = 0;

    if (!ljust) {
        for (i = 0; i < pn; i++)
            boot_putc((unsigned char)prefix[i]);
        for (i = 0; i < pad; i++)
            boot_putc(zero ? '0' : ' ');
        shim_out(body, n);
    } else {
        for (i = 0; i < pn; i++)
            boot_putc((unsigned char)prefix[i]);
        shim_out(body, n);
        for (i = 0; i < pad; i++)
            boot_putc(' ');
    }
}

int printf(const char *fmt, ...)
{
    va_list ap = {0};
    const char *p = fmt;
    int total = 0;

    va_start(ap, fmt);
    while (*p) {
        char body[32] = {0};
        char prefix[2] = {0};
        int n = 0, pn = 0, width = 0, prec = -1, ljust = 0, zero = 0, plus = 0;
        int lng = 0, base = 10, upper = 0, neg = 0;

        if (*p != '%') {
            shim_out(p++, 1);
            total++;
            continue;
        }
        p++;
        for (;;) {
            if (*p == '-')
                ljust = 1;
            else if (*p == '0')
                zero = 1;
            else if (*p == '+')
                plus = 1;
            else if (*p == ' ')
                ;
            else
                break;
            p++;
        }
        while (*p >= '0' && *p <= '9')
            width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            p++;
            prec = 0;
            while (*p >= '0' && *p <= '9')
                prec = prec * 10 + (*p++ - '0');
        }
        while (*p == 'l') {
            lng++;
            p++;
        }
        if (*p == 'z' || *p == 'h')
            p++;
        switch (*p) {
        case 'd':
        case 'i': {
            long long v = lng ? va_arg(ap, long long) : (long long)va_arg(ap, int);
            unsigned long long u = 0;
            if (v < 0) {
                neg = 1;
                u = (unsigned long long)(-v);
            } else {
                u = (unsigned long long)v;
            }
            n = fmt_digits(body, u, 10, 0);
            if (neg) {
                prefix[pn++] = '-';
            } else if (plus) {
                prefix[pn++] = '+';
            }
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            base = (*p == 'u') ? 10 : (*p == 'o') ? 8 : 16;
            upper = (*p == 'X');
            n = fmt_digits(body,
                           lng ? va_arg(ap, unsigned long long)
                               : (unsigned long long)va_arg(ap, unsigned int),
                           base, upper);
            break;
        case 'p': {
            void *v = va_arg(ap, void *);
            prefix[pn++] = '0';
            prefix[pn++] = 'x';
            n = fmt_digits(body, (unsigned long long)(size_t)v, 16, 0);
            zero = 1;
            if (width < 2 + n)
                width = 2 + n;
            break;
        }
        case 'c':
            body[0] = (char)va_arg(ap, int);
            n = 1;
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len = 0;
            if (!s)
                s = "(null)";
            while (s[len])
                len++;
            if (prec >= 0 && prec < len)
                len = prec;
            fmt_pad(s, len, width, ljust, 0, prefix, pn);
            total += len > width ? len : width;
            p++;
            continue;
        }
        case '%':
            boot_putc('%');
            total++;
            p++;
            continue;
        default:                        /* 不认识的转换：原样吐出来 */
            boot_putc('%');
            boot_putc(*p ? *p : '?');
            if (*p)
                p++;
            total += 2;
            continue;
        }
        /* 精对待整数：补前导零 */
        if (prec > n) {
            int pad = prec - n, i = 0;
            char tmp[32] = {0};
            for (i = 0; i < pad; i++)
                tmp[i] = '0';
            for (i = 0; i < n; i++)
                tmp[pad + i] = body[i];
            for (i = 0; i < pad + n; i++)
                body[i] = tmp[i];
            n += pad;
        }
        {
            int w = width > n + pn ? width : n + pn;
            fmt_pad(body, n, width, ljust, zero, prefix, pn);
            total += w;
        }
        p++;
    }
    va_end(ap);
    return total;
}

int puts(const char *s)
{
    shim_puts_(s);
    boot_putc('\n');
    return 0;
}

int putchar(int c)
{
    boot_putc(c);
    return c;
}

int vprintf(const char *fmt, va_list ap)
{
    (void)ap;                           /* 只用得到上面的 printf */
    return (int)strlen(fmt) * 0;
}

/* ---- 字符串 ---- */

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;

    while (*p)
        p++;
    return (size_t)(p - s);
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i = 0;

    for (i = 0; i < n; i++)
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    return 0;
}

char *strstr(const char *h, const char *n)
{
    size_t len = strlen(n), i = 0;

    if (!len)
        return (char *)h;
    for (; h[i]; i++)
        if (!strncmp(h + i, n, len))
            return (char *)(h + i);
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;

    while ((*d++ = *s++) != 0)
        ;
    return r;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

/* ---- qsort：插入排序，够用就行 ---- */

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    char *b = (char *)base;
    char tmp[128] = {0};
    size_t i = 0, j = 0;

    if (sz == 0 || sz > sizeof tmp)
        return;
    for (i = 1; i < n; i++) {
        memcpy(tmp, b + i * sz, sz);
        j = i;
        while (j > 0 && cmp(b + (j - 1) * sz, tmp) > 0) {
            memcpy(b + j * sz, b + (j - 1) * sz, sz);
            j--;
        }
        memcpy(b + j * sz, tmp, sz);
    }
}

/* ---- 内存里的管道：pipe/read/write/select ---- */

/* 每个 `pipe(fds)` 占一对 fd（0/1、2/3、…），没有 close 可以回收，所以这个
   数字就是一次进程里能开的管道总数：async 的每个用例各开一对，留够余量。
   容量要装得下「一次挂起几百个协程、每个写一字节」的用例（tests/async 的
   300 协程那条），所以按 1024 给。 */
#define SHIM_PIPES 16
#define SHIM_CAP 1024

struct ShimPipe {
    char buf[SHIM_CAP];
    int head, tail, count, live;
};

static struct ShimPipe shim_pipes[SHIM_PIPES] = {{0}};
static int shim_pipes_init = 0;

static struct ShimPipe *shim_pipe_of(int fd, int write_end)
{
    int pair = fd / 2;
    /* 偶数 fd 是读端，奇数 fd 是写端；两端必须一致 */
    if (fd < 0 || pair >= SHIM_PIPES)
        return 0;
    if ((fd % 2 != 0) != (write_end != 0))
        return 0;
    if (!shim_pipes[pair].live)
        return 0;
    return &shim_pipes[pair];
}

int pipe(int *fds)
{
    int i = 0;

    (void)shim_pipes_init;
    for (i = 0; i < SHIM_PIPES; i++)
        if (!shim_pipes[i].live) {
            shim_pipes[i].live = 1;
            shim_pipes[i].head = shim_pipes[i].tail = shim_pipes[i].count = 0;
            fds[0] = i * 2;
            fds[1] = i * 2 + 1;
            return 0;
        }
    return -1;
}

int write(int fd, const void *buf, int len)
{
    struct ShimPipe *p = shim_pipe_of(fd, 1);
    const char *s = (const char *)buf;
    int i = 0;

    if (!p || len < 0)
        return -1;
    for (i = 0; i < len; i++) {
        if (p->count >= SHIM_CAP)
            break;
        p->buf[p->tail] = s[i];
        p->tail = (p->tail + 1) % SHIM_CAP;
        p->count++;
    }
    return i;
}

int read(int fd, void *buf, int len)
{
    struct ShimPipe *p = shim_pipe_of(fd, 0);
    char *d = (char *)buf;
    int i = 0;

    if (!p || len < 0)
        return -1;
    for (i = 0; i < len; i++) {
        if (p->count <= 0)
            break;
        d[i] = p->buf[p->head];
        p->head = (p->head + 1) % SHIM_CAP;
        p->count--;
    }
    return i;
}

/* libtcc1 的协程泵把 fd_set 当 16 个 unsigned long 用，这里跟着同一套布局 */
int select(int nfds, void *rd, void *wr, void *ex, void *tv)
{
    unsigned long *r = (unsigned long *)rd, *w = (unsigned long *)wr;
    int fd = 0, ready = 0;

    (void)ex;
    (void)tv;
    /* 0/1/2 当控制台：永远就绪（真的 fd 1 也是这样） */
    for (fd = 0; fd < nfds && fd < 3; fd++) {
        unsigned long bit = 1UL << fd;
        int idx = fd >> 6;

        if (r && (r[idx] & bit))
            ready++;
        if (w && (w[idx] & bit))
            ready++;
    }
    for (fd = 3; fd < nfds && fd < SHIM_PIPES * 2; fd++) {
        unsigned long bit = 1UL << (fd & 63);
        int idx = fd >> 6;

        if (r && (r[idx] & bit)) {
            struct ShimPipe *p = shim_pipe_of(fd, 0);
            if (p && p->count > 0)
                ready++;
            else
                r[idx] &= ~bit;
        }
        if (w && (w[idx] & bit)) {
            struct ShimPipe *p = shim_pipe_of(fd, 1);
            if (p && p->count < SHIM_CAP)
                ready++;
            else
                w[idx] &= ~bit;
        }
    }
    return ready;
}

/* libtcc1 的协程泵优先用 epoll(7)；裸机上没有系统调用，于是走 poll(2) 这条
   退路，这里把它按内存管道实现出来（布局要和 libtcc1 的 TccPollFd 一致）。 */
struct shim_pollfd {
    int fd;
    short events;
    short revents;
};

#define SHIM_POLLIN 0x001
#define SHIM_POLLOUT 0x004

int poll(void *pfds, unsigned long nfds, int timeout)
{
    struct shim_pollfd *p = (struct shim_pollfd *)pfds;
    unsigned long i = 0;
    int ready = 0;

    (void)timeout;                      /* 裸机单线程：没有别的事可等 */
    for (i = 0; i < nfds; i++) {
        int fd = p[i].fd;
        short ev = p[i].events;

        p[i].revents = 0;
        if (fd < 0)
            continue;
        if (fd < 3) {                   /* 0/1/2 当控制台：永远就绪 */
            p[i].revents = (short)(ev & (SHIM_POLLIN | SHIM_POLLOUT));
            if (p[i].revents)
                ready++;
            continue;
        }
        if (ev & SHIM_POLLIN) {
            struct ShimPipe *q = shim_pipe_of(fd, 0);

            if (q && q->count > 0) {
                p[i].revents = (short)(p[i].revents | SHIM_POLLIN);
                ready++;
            }
        }
        if (ev & SHIM_POLLOUT) {
            struct ShimPipe *q = shim_pipe_of(fd, 1);

            if (q && q->count < SHIM_CAP) {
                p[i].revents = (short)(p[i].revents | SHIM_POLLOUT);
                ready++;
            }
        }
    }
    return ready;
}

/* ---- 剩下的都是桩子 ---- */

void abort(void)
{
    /* 裸机上没有"异常终止"：直接告诉 qemu 退出码 */
    boot_exit(2);
    for (;;)
        ;
}

int getpid(void)
{
    return 1;
}

int usleep(unsigned int us)
{
    volatile unsigned long i = 0;
    unsigned long n = (unsigned long)us * 50UL;

    for (i = 0; i < n; i++)
        ;
    return 0;
}

int stat(const char *path, void *buf)
{
    (void)path;
    memset(buf, 0, 128);                /* 够放下 x86-64 的 struct stat */
    return 0;
}

/* ---- 分配器：4MB 静态堆 + 首次适配 + 释放合并 ------------------------ *
   裸机上没人替你回收内存：不给 calloc/free 的话运行时用 libtcc1 自带的 bump
   arena（只增不减），默认 256KB 跑一整套测试就不够了。给了这三个，运行时
   就都走这里，反复创建/销毁 task 也不会漏。 */

#define SHIM_HEAP_SIZE (4 * 1024 * 1024)
#define SHIM_ALIGN 16

struct ShimHdr {
    unsigned long size;                 /* 含头部，16 对齐 */
    int used;
    struct ShimHdr *next;
};

static char shim_heap[SHIM_HEAP_SIZE] __attribute__((aligned(SHIM_ALIGN)));
static struct ShimHdr *shim_head = 0;
static int shim_heap_lock = 0;

/* 调度器一起来就会有别的任务同时进来要内存：一把原子自旋锁（单核上靠
   时钟中断保证持有者还会被调度回来，所以自旋一定会前进）。 */
int shim_xchg(int *p, int v);

__asm__(
    ".text\n"
    ".globl shim_xchg\n"
    "shim_xchg:\n"
    "  movl %esi, %eax\n"
    "  xchgl %eax, (%rdi)\n"
    "  ret\n");

static void shim_lock(void)
{
    while (shim_xchg(&shim_heap_lock, 1) != 0)
        ;
}

static void shim_unlock(void)
{
    shim_heap_lock = 0;
}

static unsigned long shim_round(unsigned long n)
{
    return (n + SHIM_ALIGN - 1) & ~(unsigned long)(SHIM_ALIGN - 1);
}

static void shim_heap_init(void)
{
    if (!shim_head) {
        shim_head = (struct ShimHdr *)(void *)shim_heap;
        shim_head->size = SHIM_HEAP_SIZE;
        shim_head->used = 0;
        shim_head->next = 0;
    }
}

void *calloc(size_t n, size_t sz)
{
    unsigned long want = 0, need = 0;
    struct ShimHdr *h = 0;

    shim_lock();
    shim_heap_init();
    want = (unsigned long)n * (unsigned long)sz;
    if (!want)
        want = 1;
    need = shim_round(want + sizeof(struct ShimHdr));
    for (h = shim_head; h; h = h->next) {
        if (h->used || h->size < need)
            continue;
        if (h->size - need >= shim_round(sizeof(struct ShimHdr) + SHIM_ALIGN)) {
            struct ShimHdr *n2 = (struct ShimHdr *)(void *)((char *)h + need);
            n2->size = h->size - need;
            n2->used = 0;
            n2->next = h->next;
            h->next = n2;
            h->size = need;
        }
        h->used = 1;
        memset((char *)h + sizeof(struct ShimHdr), 0,
               h->size - sizeof(struct ShimHdr));
        shim_unlock();
        return (char *)h + sizeof(struct ShimHdr);
    }
    shim_unlock();
    return 0;
}

void free(void *p)
{
    struct ShimHdr *h = 0, *b = 0;

    if (!p)
        return;
    shim_lock();
    h = (struct ShimHdr *)(void *)((char *)p - sizeof(struct ShimHdr));
    h->used = 0;
    while (h->next && !h->next->used) {     /* 往后合并 */
        h->size += h->next->size;
        h->next = h->next->next;
    }
    for (b = shim_head; b; b = b->next)     /* 往前合并 */
        if (!b->used && b->next == h && !h->used) {
            b->size += h->size;
            b->next = h->next;
            break;
        }
    shim_unlock();
}

void *realloc(void *p, size_t n)
{
    struct ShimHdr *h = 0;
    void *q = 0;

    if (!p)
        return calloc(1, n);
    h = (struct ShimHdr *)(void *)((char *)p - sizeof(struct ShimHdr));
    if (h->size - sizeof(struct ShimHdr) >= (unsigned long)n)
        return p;
    q = calloc(1, n);
    if (!q)
        return 0;
    memcpy(q, p, h->size - sizeof(struct ShimHdr));
    free(p);
    return q;
}
