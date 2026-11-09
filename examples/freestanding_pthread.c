/* 给 freestanding 的 c6 程序用的 pthread 桩（单线程）
 *
 * 注意：**必须用普通 C 编译，不能带 -std=c6**。每个 c6 函数开头都有
 * __tcc_defer_func_enter()，它会调 tcc_th()，而 tcc_th() 又要调
 * pthread_once()/pthread_key_create() —— 用 c6 写这层就等于在 bootstrap
 * 里递归自己，栈一冲就没了。所以它单独编成 .o 再链接：
 *
 *   ./tcc/tcc -B tcc -c -o build/freestanding_pthread.o \
 *       examples/freestanding_pthread.c
 *   ./tcc/tcc -B tcc -std=c6 -nostdlib -o build/freestanding_go \
 *       examples/freestanding_go.c build/freestanding_pthread.o tcc/libtcc1.a
 *
 * 这个桩没有真并发：pthread_create 就在当前栈上把 body 跑完，key 也只
 * 有一个槽。它证明运行时对线程层的要求就只有下面这 8 个入口；换成真的
 * clone/futex（或者目标板上的 RTOS 线程）时，注意三件事：
 *
 *   1. 每个线程要有自己的栈，pthread_create 返回后 body 才在自己线程里跑；
 *   2. pthread_key_create 的析构函数得真的在“线程退出时”调（它释放
 *      defer 栈、插值构造器、协程池，一个线程约 100 KB）；
 *   3. 一旦真的多线程，calloc/free 必须是线程安全的：libtcc1 里那个
 *      静态 arena 只在单线程下安全，所以这时请由程序提供 calloc/free。
 */
static void *key_val = 0;
static unsigned long next_id = 1;

int pthread_once(int *once, void (*fn)(void))
{
    if (!*once) {
        *once = 1;
        fn();
    }
    return 0;
}

int pthread_key_create(unsigned int *k, void (*dtor)(void *))
{
    (void)dtor;                         /* 单线程：不会走到线程退出 */
    *k = 1;
    return 0;
}

void *pthread_getspecific(unsigned int k)
{
    (void)k;
    return key_val;
}

int pthread_setspecific(unsigned int k, const void *v)
{
    (void)k;
    key_val = (void *)v;
    return 0;
}

unsigned long pthread_self(void)
{
    return 0;
}

int pthread_equal(unsigned long a, unsigned long b)
{
    return a == b;
}

int pthread_join(unsigned long tid, void **ret)
{
    (void)tid;
    if (ret)
        *ret = 0;
    return 0;
}

int pthread_create(unsigned long *tid, const void *attr,
                   void *(*fn)(void *), void *arg)
{
    (void)attr;
    *tid = next_id++;
    fn(arg);                            /* 同步跑：这里没有真并发 */
    return 0;
}
