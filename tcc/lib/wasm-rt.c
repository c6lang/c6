/* wasm 目标的运行期配套（只在 --target=wasm* 的库里编进来）。
 *
 * 两件事：
 *
 *   1. ILP32 目标（wasm32）的 ABI 适配层。LLVM 后端输出的 IR 一直是 LP64
 *      模型：unsigned long、size_t、指针在 IR 里都是 i64（见
 *      docs/llvm-backend.md）。wasm32 的 C ABI 里它们是 32 位，所以
 *      libtcc1.c 用 -D__c6_gc_new=__c6_gc_new_lp64 这一类改名编出 32 位
 *      版本，这里再提供 IR 期望的 64 位签名去调用它。wasm64 的 long 和指针
 *      本来就是 64 位，这一段整个不编。
 *
 *   2. libc / 线程的最小桩。wasm 模块不链接 libc（-nostdlib），可运行库是
 *      普通 C，还是要 calloc、free、pthread_* 和 __cxa_atexit。
 */
#ifdef __wasm__

#include <stddef.h>
#include <stdint.h>

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

/* ---------------- 2. libc / 线程桩 ---------------- */

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

/* wasm 没有线程：多线程相关入口一律“没有线程可等” */
typedef unsigned long pthread_t;
typedef unsigned int pthread_key_t;
typedef int pthread_once_t;

int pthread_create(pthread_t *t, const void *a, void *(*fn)(void *), void *arg)
{
    (void)t; (void)a; (void)fn; (void)arg;
    return -1;
}
int pthread_join(pthread_t t, void **r) { (void)t; (void)r; return 0; }
pthread_t pthread_self(void) { return 0; }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }
void *pthread_getspecific(pthread_key_t k) { (void)k; return 0; }
int pthread_setspecific(pthread_key_t k, const void *v)
{
    (void)k; (void)v;
    return 0;
}
int pthread_key_create(pthread_key_t *k, void (*d)(void *))
{
    (void)d;
    if (k)
        *k = 1;
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
