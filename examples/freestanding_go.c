/* freestanding 下的 go / thread：程序自己实现 pthread（真线程）
 *
 *   make fstest-go
 *
 * examples/freestanding_pthread.c 用 clone + futex 实现那 8 个入口（普通 C，
 * 不是 c6），这里只写 c6：go、thread 句柄、h->join()、h->value()，以及在线程
 * 里用字符串插值（每个线程有自己的 defer 栈、错误槽和插值构造器）。
 *
 * 下面 spinner / setter 的配合只有真并发才能成立：spinner 要等到 setter 置位
 * 才返回 1，单线程的桩会转到上限返回 0。
 */
static void w(const char *s, unsigned long n)
{
    __asm__ __volatile__("syscall"
                         :
                         : "a"((long)1), "D"((long)1), "S"(s), "d"(n)
                         : "rcx", "r11", "memory");
}

static void x(int code)
{
    __asm__ __volatile__("syscall" : : "a"((long)60), "D"((long)code));
    for (;;)
        ;
}

static void puts_(const char *s)
{
    unsigned long n = 0;

    while (s[n])
        ++n;
    w(s, n);
}

static volatile int flag = 0;

static int spinner(int n)
{
    int i = 0;

    puts_(f"spinner {n}: waiting\n");    /* 线程里照常写 f".." */
    while (!flag && i < 200000000)
        i++;
    return flag ? 1 : 0;
}

static int setter(int n)
{
    flag = 1;
    puts_(f"setter {n}: done\n");
    return n * 10;
}

void _start(void)
{
    int rc = 0;
    thread a = go spinner(1);
    thread b = go setter(2);

    if (!a || !b) {
        rc = 4;                         /* 链接里没有 pthread 层时走这里 */
    } else {
        a->join();
        b->join();
        rc |= a->value() == 1 ? 0 : 1;  /* 只有真并发才拿得到 1 */
        rc |= b->value() == 20 ? 0 : 2;
    }
    puts_(rc ? "go: FAILED\n" : "go: ok\n");
    x(rc);
}
