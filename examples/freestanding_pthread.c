/* freestanding 的 pthread：x86-64 Linux 上直接 clone + futex，不用 libc
 *
 * 这个文件是普通 C（**不能带 -std=c6**，见文件末尾的说明），单独编成 .o
 * 再和 c6 程序链接：
 *
 *   ./tcc/tcc -B tcc -c -o build/freestanding_pthread.o \
 *       examples/freestanding_pthread.c
 *   ./tcc/tcc -B tcc -std=c6 -nostdlib -o build/freestanding_go \
 *       examples/freestanding_go.c build/freestanding_pthread.o tcc/libtcc1.a
 *
 * 运行时只要 8 个入口（都是 weak 引用）：pthread_once / pthread_key_create /
 * pthread_getspecific / pthread_setspecific / pthread_create / pthread_join /
 * pthread_self / pthread_equal。这里有真线程，不是桩。
 *
 * 实现要点
 * --------
 * 1. 线程指针放在 %fs:0（arch_prctl(ARCH_SET_FS)），因为运行时不用 TLS，
 *    一个寄存器就够；程序自己别再用 __thread，否则会打架。
 * 2. pthread_create 做三件 syscall：mmap 一块（栈 + 控制块）、clone、
 *    由子线程设置自己的 %fs。clone 的栈顶 16 字节对齐，子线程从
 *    fs_child 开始、`call fs_child_main` 进去，正好是 C 的调用约定。
 * 3. 线程退出用 SYS_exit（60），带 CLONE_THREAD 时只结束这个线程。
 * 4. join 用 futex：clone 时带 CLONE_CHILD_CLEARTID，内核在子线程**真正退出**
 *    时把 ctid 字清零并唤醒等待者。父线程在 clone 之前先把它写成 -1（活着），
 *    于是 join 就是“ktid != 0 就 FUTEX_WAIT”，醒来再 munmap 那块。
 *    这里不能用 CLONE_CHILD_SETTID 当“活着”的标志：那个 tid 是子线程开始跑的
 *    时候才写的，父线程可能已经看到 0 —— 于是 join 提前返回、把子线程还要用
 *    的栈 munmap 掉，子线程一上来就段错误（这个坑踩过）。
 * 5. 分配器：真线程下运行时需要一个线程安全的 calloc，libtcc1 里那个静态
 *    arena 只适合单线程，所以这里用 mmap 直接给（慢，但不用锁）。
 */

/* ---- 系统调用：需要 r10/r8/r9 的都写成全局 asm ---------------------- *
   编译器内联 asm 里 TCC 只认 a/b/c/d/S/D/r/m 这些约束，没有 r10/r8/r9，
   所以第 4 个以后的参数在 asm 里自己搬。 */

long fs_write(long fd, const void *buf, long n);
long fs_mmap(long len);
long fs_munmap(void *p, long len);
long fs_futex(long addr, long op, int val);
long fs_set_fs(void *tcb);
void *fs_get_fs(void);
void fs_exit_thread(int code) __attribute__((noreturn));
long fs_clone(void *tcb, void *stack_top, int *ctid);
void fs_child_main(void *tcb) __attribute__((noreturn));

__asm__(
    ".text\n"
    ".globl fs_write\n"
    "fs_write:\n"                       /* (fd, buf, n) */
    "  movq $1, %rax\n"
    "  syscall\n"
    "  ret\n"
    ".globl fs_mmap\n"
    "fs_mmap:\n"                        /* (len) -> 匿名 RW 映射 */
    "  movq %rdi, %rsi\n"
    "  xorl %edi, %edi\n"
    "  movq $3, %rdx\n"                 /* PROT_READ|PROT_WRITE */
    "  movq $0x22, %r10\n"              /* MAP_PRIVATE|MAP_ANONYMOUS */
    "  movq $-1, %r8\n"
    "  xorl %r9d, %r9d\n"
    "  movq $9, %rax\n"
    "  syscall\n"
    "  ret\n"
    ".globl fs_munmap\n"
    "fs_munmap:\n"                      /* (p, len) */
    "  movq $11, %rax\n"
    "  syscall\n"
    "  ret\n"
    ".globl fs_futex\n"
    "fs_futex:\n"                       /* (addr, op, val)，timeout = NULL */
    "  movq $202, %rax\n"
    "  xorl %r10d, %r10d\n"
    "  syscall\n"
    "  ret\n"
    ".globl fs_set_fs\n"
    "fs_set_fs:\n"                      /* (tcb) arch_prctl(ARCH_SET_FS) */
    "  movq %rdi, %rsi\n"
    "  movq $0x1002, %rdi\n"
    "  movq $158, %rax\n"
    "  syscall\n"
    "  ret\n"
    ".globl fs_get_fs\n"
    "fs_get_fs:\n"
    "  movq %fs:0, %rax\n"
    "  ret\n"
    ".globl fs_exit_thread\n"
    "fs_exit_thread:\n"                 /* (code) 只结束当前线程 */
    "  movq $60, %rax\n"
    "  syscall\n"
    "  hlt\n"
    ".globl fs_clone\n"
    "fs_clone:\n"                       /* (tcb, stack_top, ctid) */
    "  movq %rdi, %r12\n"               /* 子线程用它找自己的控制块 */
    "  movq %rdx, %r10\n"               /* ctid */
    "  movq %rsi, %rsi\n"               /* child_stack */
    "  movq $0x250f00, %rdi\n"          /* VM|FS|FILES|SIGHAND|THREAD|
                                           SYSVSEM|CHILD_CLEARTID */
    "  xorl %edx, %edx\n"               /* ptid = NULL */
    "  xorl %r8d, %r8d\n"               /* tls = 0 */
    "  movq $56, %rax\n"
    "  syscall\n"
    "  testq %rax, %rax\n"
    "  jz 1f\n"                         /* 0：这是子线程 */
    "  ret\n"                           /* 父线程：rax = 子线程的 tid */
    "1:\n"
    "  movq %r12, %rdi\n"
    "  call fs_child_main\n"            /* 新栈上，C 调用约定 */
    "  hlt\n");

/* ---- 线程控制块 ------------------------------------------------------ */

#define FS_STACK (256UL << 10)
#define FS_KEYS  8

typedef struct FsTcb {
    void *self;
    int ktid;                           /* 内核 tid；退出时被内核清零 */
    void *(*fn)(void *);
    void *arg;
    void *ret;
    char *block;                        /* 栈 + 这个结构体，join 时 munmap */
    unsigned long len;
    void *keys[FS_KEYS];
} FsTcb;

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static void (*fs_dtor[FS_KEYS])(void *);
static int fs_key_next = 1;
static FsTcb fs_main_tcb;
static int fs_tls_ready = 0;

/* xchg：原子地换一个 int（TCC 没有 __sync_* 内建，用一句 asm） */
static int fs_xchg(volatile int *p, int v)
{
    int old = v;

    __asm__ __volatile__("xchgl %0, %1"
                         : "=r"(old), "+m"(*p) : "0"(old) : "memory");
    return old;
}

static FsTcb *fs_self(void)
{
    if (!fs_tls_ready) {                /* 主线程第一次用到 */
        fs_main_tcb.self = &fs_main_tcb;
        fs_set_fs(&fs_main_tcb);
        fs_tls_ready = 1;
    }
    return (FsTcb *)fs_get_fs();
}

/* ---- 8 个入口 -------------------------------------------------------- */

int pthread_once(int *once, void (*fn)(void))
{
    if (*(volatile int *)once == 2)
        return 0;
    if (fs_xchg((volatile int *)once, 1) == 0) {
        fn();
        *(volatile int *)once = 2;
        fs_futex((long)once, FUTEX_WAKE, 1 << 30);
    } else {
        while (*(volatile int *)once != 2)
            fs_futex((long)once, FUTEX_WAIT, 1);
    }
    return 0;
}

int pthread_key_create(unsigned int *key, void (*dtor)(void *))
{
    int k = fs_key_next;

    if (k >= FS_KEYS)
        return -1;                      /* 8 个 key 够运行时用了 */
    fs_key_next = k + 1;
    fs_dtor[k] = dtor;
    *key = (unsigned int)k;
    return 0;
}

void *pthread_getspecific(unsigned int key)
{
    FsTcb *t = fs_self();

    if (!t || key >= FS_KEYS)
        return 0;
    return t->keys[key];
}

int pthread_setspecific(unsigned int key, const void *val)
{
    FsTcb *t = fs_self();

    if (!t || key >= FS_KEYS)
        return -1;
    t->keys[key] = (void *)val;
    return 0;
}

unsigned long pthread_self(void)
{
    return (unsigned long)fs_self();
}

int pthread_equal(unsigned long a, unsigned long b)
{
    return a == b;
}

int pthread_create(unsigned long *tid, const void *attr,
                   void *(*fn)(void *), void *arg)
{
    unsigned long len = FS_STACK + sizeof(FsTcb) + 64;
    char *block = 0;
    FsTcb *t = 0;
    unsigned long top = 0;

    (void)attr;
    block = (char *)fs_mmap((long)len);
    if ((long)block <= 0)
        return -1;
    t = (FsTcb *)(block + len - sizeof(FsTcb));
    t->self = t;
    t->ktid = -1;                       /* 非 0 = 还活着，见上面第 4 条 */
    t->fn = fn;
    t->arg = arg;
    t->ret = 0;
    t->block = block;
    t->len = len;
    for (int i = 0; i < FS_KEYS; i++)
        t->keys[i] = 0;
    top = (unsigned long)t & ~15UL;     /* 栈顶 16 字节对齐，往下长 */
    if (fs_clone(t, (void *)top, &t->ktid) < 0) {
        fs_munmap(block, (long)len);
        return -1;
    }
    *tid = (unsigned long)t;
    return 0;
}

void fs_child_main(void *arg)
{
    FsTcb *t = (FsTcb *)arg;
    int i;

    fs_set_fs(t);                       /* 先认自己，之后才能碰运行时 */
    t->ret = t->fn(t->arg);
    for (i = 1; i < FS_KEYS; i++)       /* 相当于 pthread 的 key 析构 */
        if (fs_dtor[i])
            fs_dtor[i](t->keys[i]);
    fs_exit_thread(0);
}

int pthread_join(unsigned long tid, void **ret)
{
    FsTcb *t = (FsTcb *)tid;

    if (!t)
        return -1;
    if (t == fs_self())
        return -1;                      /* 自己 join 自己 */
    while (*(volatile int *)&t->ktid != 0)
        fs_futex((long)&t->ktid, FUTEX_WAIT, t->ktid);
    if (ret)
        *ret = t->ret;
    fs_munmap(t->block, (long)t->len);
    return 0;
}

/* ---- 线程安全的分配器（libtcc1 的静态 arena 只适合单线程） ---------- */

void *calloc(unsigned long n, unsigned long size)
{
    unsigned long len = n * size, total = 0;
    unsigned long *h = 0;

    if (!len)
        len = 1;
    total = len + 32;
    h = (unsigned long *)fs_mmap((long)total);
    if ((long)h <= 0)
        return 0;                       /* mmap 的页本来就是 0 */
    h[0] = total;
    h[1] = len;
    return (char *)h + 32;
}

void free(void *p)
{
    if (p)
        fs_munmap((char *)p - 32, (long)((unsigned long *)p)[-2]);
}

void *realloc(void *p, unsigned long n)
{
    unsigned long old = 0, i = 0;
    char *np = 0, *op = (char *)p;

    if (!p)
        return calloc(1, n);
    old = ((unsigned long *)p)[-1];
    if (n <= old)
        return p;
    np = (char *)calloc(1, n);
    if (!np)
        return 0;
    for (i = 0; i < old; i++)
        np[i] = op[i];
    free(p);
    return np;
}
