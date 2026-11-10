/* 裸机 + 多核：`spawn` 出来的任务在另一个核上跑，两个核真的同时在推进。
 *
 * 举证的办法（不是"看着像"）：
 *   - 每个任务自己报"我在哪个核"——核号是从 LAPIC 的 ID 寄存器读的；
 *   - A 把**本核中断关掉**再空转一段：这段时间里本核绝不可能切到别的任务，
 *     可 B 的计数器还在涨 —— 那只可能是另一个核在同时跑它；
 *   - 单核启动（SMP=1）时同一个判定会给出"没涨"，这是有意的负对照。
 */

void sched_init(void);
int smp_start_aps(int ncpu);
int sched_cpu_count(void);
int sched_core(void);
unsigned long sched_switches(void);
int sched_xchg(int *p, int v);
void sched_cli(void);
void sched_sti(void);
void boot_exit(int code);
int printf(const char *fmt, ...);

#ifndef C6_SMP_CPUS
#define C6_SMP_CPUS 2
#endif

#define CAP 400000000UL
#define WINDOW 2000000UL

static volatile int a_core = -1;
static volatile int b_core = -1;
static volatile int b_in = 0;
static volatile int window_done = 0;
static volatile unsigned long a_prog = 0;
static volatile unsigned long b_prog = 0;
static volatile unsigned long b_delta = 0;
static int print_lock = 0;

static void say2(const char *fmt, int a, int b)
{
    while (sched_xchg(&print_lock, 1))
        ;
    printf(fmt, a, b);
    sched_xchg(&print_lock, 0);
}

/* 任务 B：报完名就一直推进计数器，直到 A 的观察窗口关上 */
static int worker_b(int arg)
{
    b_core = sched_core();
    say2("smp: B 跑在核 %d（它自己从 LAPIC 读的）\n", b_core, 0);
    b_in = 1;
    while (!window_done && b_prog < CAP)
        b_prog++;
    return 2;
}

void _start(void)
{
    int ncpu = 0;
    int ok = 0;
    unsigned long i = 0, p0 = 0, p1 = 0;

    sched_init();
    ncpu = smp_start_aps(C6_SMP_CPUS);   /* 拉起 AP；返回在线核数 */
    a_core = sched_core();
    say2("smp: 在线核数 %d，A 跑在核 %d\n", ncpu, a_core);

    spawn worker_b(2);                   /* 有多核时这活会落到 AP 上 */

    while (!b_in && a_prog < CAP)        /* 等 B 报到 */
        a_prog++;

    /* 关键一步：关掉本核中断。本核从此不可能切到别的任务，
       这时 B 的计数器要是还在涨，就只可能是另一个核在跑它。 */
    sched_cli();
    p0 = b_prog;
    for (i = 0; i < WINDOW; i++)
        a_prog++;
    p1 = b_prog;
    window_done = 1;
    sched_sti();
    b_delta = p1 - p0;

    if (ncpu >= 2 && a_core != b_core && p1 != p0)
        ok = 1;
    else if (ncpu < 2 && a_core == b_core && p1 == p0)
        ok = 2;                          /* 单核负对照：本来就该不涨 */

    say2("smp: A 关中断空转期间 B 的计数器涨了 %d\n", (int)b_delta, 0);
    printf("smp: A 在核 %d，B 在核 %d，时钟切换 %d 次\n",
           a_core, b_core, (int)sched_switches());
    if (ok == 1)
        printf("smp: 真并行 —— 两个任务同时跑在两个核上\n");
    else if (ok == 2)
        printf("smp: 单核，只有交错没有并行（负对照，符合预期）\n");
    else
        printf("smp: 没有并行\n");
    boot_exit(ok ? 0 : 3);
}
