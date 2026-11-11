/* wasm 目标的运行期配套（只在 --target=wasm* 的库里编进来）。
 *
 * 四件事：
 *
 *   1. ILP32 目标（wasm32）的 ABI 适配层。LLVM 后端输出的 IR 一直是 LP64
 *      模型：unsigned long、size_t、指针在 IR 里都是 i64（见
 *      docs/reference/llvm-backend.md）。wasm32 的 C ABI 里它们是 32 位，所以
 *      libtcc1.c 用 -D__c6_gc_new=__c6_gc_new_lp64 这一类改名编出 32 位
 *      版本，这里再提供 IR 期望的 64 位签名去调用它。wasm64 的 long 和指针
 *      本来就是 64 位，这一段整个不编。
 *
 *   2. libc 的最小桩。wasm 模块不链接 libc（-nostdlib），可运行库是普通 C，
 *      还是要 calloc、free 和 __cxa_atexit。
 *
 *   3. 控制台。模块没有 fd 可写，所以 printf 系列把字符攒进一块静态缓冲区，
 *      JS 那边用 __c6_wasm_con_ptr() / __c6_wasm_con_len() 读出来再打印（见
 *      scripts/wasm-smoke.sh 里的 runner）。格式化时的实参一律按 64 位读：
 *      后端输出的 IR 把所有实参都提升成 i64 传（LP64 模型），和宿主 libc 的
 *      va_arg 布局对不上，照抄 glibc 的写法读出来的第二个实参就是错的。
 *
 *   4. 线程。wasm 没有线程，pthread_create() 就在当前“线程”里把函数体跑完、
 *      把返回值记下来给 pthread_join() —— spawn 的任务因此真的会执行（只是
 *      顺序一致、不并发），__c6_th_join_all() 也有的可等。
 */
#ifdef __wasm__

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* ---------------- 1. LP64 -> ILP32 适配（只在 wasm32 需要） ---------------- */
#ifndef __wasm64__

void *__c6_gc_new_lp64(unsigned long size, unsigned long count,
                       const void *desc, int flags);
void *__c6_gc_new(int64_t size, int64_t count, const void *desc, int flags)
{
    return __c6_gc_new_lp64((unsigned long)size, (unsigned long)count,
                            desc, flags);
}

void __c6_gc_root_lp64(void *block, int kind, unsigned long count,
                       unsigned long stride);
void __c6_gc_root(void *block, int kind, int64_t count, int64_t stride)
{
    __c6_gc_root_lp64(block, kind, (unsigned long)count, (unsigned long)stride);
}

void __c6_fmt_l_lp64(const char *s, unsigned long n);
void __c6_fmt_l(const char *s, int64_t n)
{
    __c6_fmt_l_lp64(s, (unsigned long)n);
}

void __c6_fmt_s_lp64(const char *s, unsigned long n, int spec);
void __c6_fmt_s(const char *s, int64_t n, int spec)
{
    __c6_fmt_s_lp64(s, (unsigned long)n, spec);
}

/* 切片的两个帮助函数不能直接转调 libtcc1.c 里的实现：那边的 len 位置是
   'sizeof(void *)'（wasm32 上是 4），而 IR 里切片恒为 { ptr, i64 len } 的
   16 字节 LP64 布局，len 在偏移 8。这里按 IR 的布局重写一遍。越界照样是
   空指针写入，wasm 上会 trap。 */
static void wasm_slice_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

int __tcc_slice_index(int64_t out, int64_t slice, int64_t idx, int64_t esz)
{
    const char *base = *(const char *const *)(intptr_t)slice;
    uint64_t len = *(const uint64_t *)((intptr_t)slice + 8);

    if ((uint64_t)idx >= len)
        wasm_slice_trap();
    *(void **)(intptr_t)out = (void *)(intptr_t)(base + (int64_t)idx * esz);
    return 0;
}

int __tcc_slice_sub(int64_t out, int64_t slice, int64_t lo, int64_t hi,
                    int64_t esz)
{
    const char *base = *(const char *const *)(intptr_t)slice;
    uint64_t len = *(const uint64_t *)((intptr_t)slice + 8);

    if (lo < 0 || lo > hi || (uint64_t)hi > len)
        wasm_slice_trap();
    *(void **)(intptr_t)out = (void *)(intptr_t)(base + lo * esz);
    *(uint64_t *)((intptr_t)out + 8) = (uint64_t)(hi - lo);
    return 0;
}

/* 下面这几个在 libtcc1.c 里返回 void，IR 却按“有返回值”调用（值没人读），
   所以适配层统一返回 0。 */
void __tcc_err_flag_peek_lp64(int *out);
int __tcc_err_flag_peek(int64_t out)
{
    __tcc_err_flag_peek_lp64((int *)(intptr_t)out);
    return 0;
}

void __tcc_err_set_copy_lp64(int id, const char *msg);
int __tcc_err_set_copy(int64_t id, int64_t msg)
{
    __tcc_err_set_copy_lp64((int)id, (const char *)(intptr_t)msg);
    return 0;
}

void __tcc_iface_init_lp64(void *self, const void *vt, void *dst);
int __tcc_iface_init(int64_t self, int64_t vt, int64_t dst)
{
    __tcc_iface_init_lp64((void *)(intptr_t)self, (const void *)(intptr_t)vt,
                          (void *)(intptr_t)dst);
    return 0;
}

void *__tcc_sp_args_lp64(unsigned long size);
void *__tcc_sp_args(int64_t size)
{
    return __tcc_sp_args_lp64((unsigned long)size);
}

#endif /* !__wasm64__ */

/* ---------------- 2. 内存 / 退出 ---------------- */

static unsigned char wasm_pool[4u << 20];
static size_t wasm_used;

void *malloc(size_t n)
{
    size_t a = (n + 15u) & ~(size_t)15u;

    if (wasm_used + a > sizeof wasm_pool)
        return 0;
    {
        void *p = wasm_pool + wasm_used;
        wasm_used += a;
        return p;
    }
}

void *calloc(size_t n, size_t s)
{
    size_t t = n * s;
    unsigned char *p = malloc(t);
    size_t i;

    if (p)
        for (i = 0; i < t; i++)
            p[i] = 0;
    return p;
}

void free(void *p) { (void)p; }

void *realloc(void *p, size_t n)
{
    void *q = malloc(n);
    size_t i;

    if (p && q)
        for (i = 0; i < n; i++)
            ((unsigned char *)q)[i] = ((unsigned char *)p)[i];
    return q;
}

int __cxa_atexit(void (*f)(void *), void *a, void *d)
{
    (void)f; (void)a; (void)d;
    return 0;
}

/* ---------------- 3. 控制台 ---------------- */

#define WASM_CON_MAX (64u << 10)

static char wasm_con[WASM_CON_MAX];
static unsigned wasm_con_len;

/* 给 JS 的三个入口（-Wl,--export-all 会把它们导出去） */
long long __c6_wasm_con_ptr(void)
{
    return (long long)(intptr_t)wasm_con;
}

long long __c6_wasm_con_len(void)
{
    return (long long)wasm_con_len;
}

void __c6_wasm_con_clear(void)
{
    wasm_con_len = 0;
}

struct wasm_out {
    unsigned len;
};

static void wasm_putc(struct wasm_out *o, char c)
{
    if (wasm_con_len + 1 < WASM_CON_MAX)
        wasm_con[wasm_con_len++] = c;
    o->len++;
}

static void wasm_puts_in(struct wasm_out *o, const char *s, unsigned max)
{
    if (!s)
        s = "(null)";
    while (*s && max--)
        wasm_putc(o, *s++);
}

/* 数字：radix 10 / 16；符号由调用者先打出去，PREC 是最少位数 */
static void wasm_putn(struct wasm_out *o, unsigned long long v, int radix,
                      int upper, int width, char pad, int left, int prec)
{
    char tmp[24];
    int n = 0, zeros = 0, i, printed;
    const char *tab = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (!(radix == 16 && v == 0 && prec == 0))
        do {
            tmp[n++] = tab[(unsigned)(v % (unsigned)radix)];
            v /= (unsigned)radix;
        } while (v);
    if (prec > n)
        zeros = prec - n;
    printed = zeros + n;
    if (!left && pad == ' ')
        for (i = printed; i < width; i++)
            wasm_putc(o, ' ');
    for (i = 0; i < zeros; i++)
        wasm_putc(o, '0');
    if (!left && pad == '0')
        for (i = printed; i < width; i++)
            wasm_putc(o, '0');
    while (n--)
        wasm_putc(o, tmp[n]);
    if (left)
        for (i = printed; i < width; i++)
            wasm_putc(o, ' ');
}

static void wasm_putf(struct wasm_out *o, double v, int prec, int width,
                      int left)
{
    char frac[16];
    unsigned long long ip, scale = 1, fv;
    int neg = 0, i, idig = 1, len;

    if (prec < 0)
        prec = 6;
    if (prec > 12)
        prec = 12;
    if (v != v) {                       /* NaN */
        wasm_puts_in(o, "nan", 3);
        return;
    }
    if (v < 0) {
        neg = 1;
        v = -v;
    }
    if (v > 1e18)
        v = 1e18;
    ip = (unsigned long long)v;
    for (i = 0; i < prec; i++)
        scale *= 10;
    fv = (unsigned long long)((v - (double)ip) * (double)scale + 0.5);
    if (fv >= scale) {                  /* 四舍五入进位到整数部分 */
        fv -= scale;
        ip++;
    }
    for (i = prec - 1; i >= 0; i--) {
        frac[i] = (char)('0' + (int)(fv % 10));
        fv /= 10;
    }
    {
        unsigned long long t = ip;
        while (t >= 10) {
            t /= 10;
            idig++;
        }
    }
    len = (neg ? 1 : 0) + idig + (prec ? prec + 1 : 0);
    if (!left)
        for (i = len; i < width; i++)
            wasm_putc(o, ' ');
    if (neg)
        wasm_putc(o, '-');
    wasm_putn(o, ip, 10, 0, 0, ' ', 0, 0);
    if (prec) {
        wasm_putc(o, '.');
        for (i = 0; i < prec; i++)
            wasm_putc(o, frac[i]);
    }
    if (left)
        for (i = len; i < width; i++)
            wasm_putc(o, ' ');
}

static void wasm_vfmt(struct wasm_out *o, const char *fmt, va_list *ap)
{
    const char *p = fmt;

    while (p && *p) {
        int left = 0, zero = 0, width = 0, prec = -1, longs = 0;
        char c = *p++;

        if (c != '%') {
            wasm_putc(o, c);
            continue;
        }
        for (; *p == '-' || *p == '+' || *p == ' ' || *p == '#'; p++)
            if (*p == '-')
                left = 1;
        if (*p == '0') {
            zero = 1;
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
        while (*p == 'l' || *p == 'h' || *p == 'z' || *p == 'j' || *p == 't'
               || *p == 'L') {
            if (*p == 'l')
                longs++;
            p++;
        }
        /* 实参一律按 64 位读：IR 把每个实参都提升成 i64 传 */
        switch (c = *p ? *p++ : 0) {
        case 'd':
        case 'i': {
            long long v = va_arg(*ap, long long);
            unsigned long long mag;
            int w = width;
            char pad = zero && !left ? '0' : ' ';

            if (longs == 0)
                v = (int)v;             /* 源码里是 int，IR 提升到了 i64 */
            if (v < 0) {
                wasm_putc(o, '-');
                mag = (unsigned long long)(-(v + 1)) + 1u;
                if (w > 0)
                    w--;
            } else {
                mag = (unsigned long long)v;
            }
            wasm_putn(o, mag, 10, 0, w, pad, left, prec);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long v = va_arg(*ap, unsigned long long);
            if (longs == 0)
                v = (unsigned int)v;
            wasm_putn(o, v, c == 'u' ? 10 : 16, c == 'X', width,
                      zero && !left ? '0' : ' ', left, prec);
            break;
        }
        case 'c':
            wasm_putc(o, (char)(int)va_arg(*ap, long long));
            break;
        case 's': {
            const char *s = (const char *)(intptr_t)va_arg(*ap, long long);
            unsigned n = 0;
            int k;
            if (!s)
                s = "(null)";
            while (s[n] && (prec < 0 || n < (unsigned)prec))
                n++;
            if (!left)
                for (k = (int)n; k < width; k++)
                    wasm_putc(o, ' ');
            wasm_puts_in(o, s, n);
            if (left)
                for (k = (int)n; k < width; k++)
                    wasm_putc(o, ' ');
            break;
        }
        case 'p':
            wasm_puts_in(o, "0x", 2);
            wasm_putn(o, va_arg(*ap, unsigned long long), 16, 0, 0, ' ', 0, 0);
            break;
        case 'f':
        case 'F':
        case 'g':
        case 'G': {
            union {
                unsigned long long i;
                double d;
            } u;

            u.i = va_arg(*ap, unsigned long long);
            wasm_putf(o, u.d, prec, width, left);
            break;
        }
        case '%':
            wasm_putc(o, '%');
            break;
        case 0:
            return;
        default:
            wasm_putc(o, '%');
            wasm_putc(o, c);
            break;
        }
    }
}

/* clang 有 printf 系列的内建声明，可这里没有 <stdio.h> 可包含（wasm 目标不带
   libc 头），签名是照着 IR 的调用点手写的，所以关掉这条提示 */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wbuiltin-requires-header"
#endif

int printf(const char *fmt, ...)
{
    struct wasm_out o;
    va_list ap;

    o.len = 0;
    va_start(ap, fmt);
    wasm_vfmt(&o, fmt, &ap);
    va_end(ap);
    return (int)o.len;
}

int puts(const char *s)
{
    struct wasm_out o;

    o.len = 0;
    wasm_puts_in(&o, s, ~0u);
    wasm_putc(&o, '\n');
    return (int)o.len;
}

int putchar(int c)
{
    struct wasm_out o;

    o.len = 0;
    wasm_putc(&o, (char)c);
    return c;
}

int fputs(const char *s, void *f)
{
    struct wasm_out o;

    (void)f;
    o.len = 0;
    wasm_puts_in(&o, s, ~0u);
    return (int)o.len;
}

int fprintf(void *f, const char *fmt, ...)
{
    struct wasm_out o;
    va_list ap;

    (void)f;
    o.len = 0;
    va_start(ap, fmt);
    wasm_vfmt(&o, fmt, &ap);
    va_end(ap);
    return (int)o.len;
}

int fflush(void *f)
{
    (void)f;
    return 0;
}

/* 没有时钟可等，睡多少都立刻返回 */
int usleep(unsigned int usec)
{
    (void)usec;
    return 0;
}

unsigned int sleep(unsigned int sec)
{
    (void)sec;
    return 0;
}

/* ---------------- 4. 线程 ---------------- */

typedef unsigned long pthread_t;
typedef unsigned int pthread_key_t;
typedef int pthread_once_t;

#define WASM_TH_MAX 64
#define WASM_KEY_MAX 8

static void *wasm_th_ret[WASM_TH_MAX];
static unsigned wasm_th_next;
static void *wasm_key_val[WASM_KEY_MAX];
static unsigned wasm_key_next;

/* 没有真线程：函数体在这里就地跑完，返回值留给 pthread_join()。spawn 的任务
   因此真的会执行（顺序一致），只是不并发。 */
int pthread_create(pthread_t *t, const void *a, void *(*fn)(void *), void *arg)
{
    unsigned id;
    void *ret;

    (void)a;
    ret = fn(arg);
    id = wasm_th_next++ % WASM_TH_MAX;
    wasm_th_ret[id] = ret;
    if (t)
        *t = id;
    return 0;
}

int pthread_join(pthread_t t, void **r)
{
    if (r)
        *r = wasm_th_ret[t % WASM_TH_MAX];
    return 0;
}

pthread_t pthread_self(void) { return 0; }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

void *pthread_getspecific(pthread_key_t k)
{
    return wasm_key_val[k % WASM_KEY_MAX];
}

int pthread_setspecific(pthread_key_t k, const void *v)
{
    wasm_key_val[k % WASM_KEY_MAX] = (void *)(intptr_t)v;
    return 0;
}

int pthread_key_create(pthread_key_t *k, void (*d)(void *))
{
    (void)d;
    if (k)
        *k = wasm_key_next++ % WASM_KEY_MAX;
    return 0;
}

int pthread_once(pthread_once_t *o, void (*f)(void))
{
    if (o && *o == 0) {
        *o = 1;
        f();
    }
    return 0;
}

int select(int n, void *r, void *w, void *e, void *t)
{
    (void)n; (void)r; (void)w; (void)e; (void)t;
    return 0;
}

#endif /* __wasm__ */
