/* freestanding 下的 go / thread：程序自己实现 pthread 层
 *
 *   make fstest-go
 *
 * examples/freestanding_pthread.c 是那层桩（普通 C，不是 c6），这里只用
 * go / thread / h->join() / h->value()。运行时对线程的要求就是 8 个入口：
 *
 *   pthread_once / pthread_key_create / pthread_getspecific /
 *   pthread_setspecific / pthread_create / pthread_join /
 *   pthread_self / pthread_equal
 *
 * 都给了（强定义），tcc_th() 就从“一个静态状态块”切回“每线程一块”，
 * __tcc_th_spawn 也会真的去 create —— 不需要改运行时任何东西。
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

static int work(int n)
{
    puts_(f"body {n}\n");               /* 线程里一样能用插值 */
    return n * 10;
}

void _start(void)
{
    int rc = 0;
    thread h = go work(5);

    go work(4);                         /* 不取句柄：启动者退出时回收 */
    if (!h) {
        rc = 2;                         /* 链接里没有 pthread 时走这里 */
    } else {
        h->join();
        rc |= h->value() == 50 ? 0 : 1;
    }
    puts_(rc ? "go: FAILED\n" : "go: ok\n");
    x(rc);
}
