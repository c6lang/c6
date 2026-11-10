/* 裸机上的 spawn：单核抢占式交错 —— 真并发，不是"顺序跑一遍"。
 *
 * 两个任务谁都**没有让出语句**，而且互相等：空转的那个一开始就置 started，
 * 然后死等 go_flag；另一个先等 started，然后把 go_flag 立起来。
 *
 *   - 抢占式：时钟中断在任意指令边界把 CPU 抢走，于是空转的那个会被打断，
 *     另一个才有机会跑起来把 go_flag 立起来 —— 空转的那个于是很早就退出；
 *   - 不是抢占式（比如 spawn 只是在同一个栈上顺序调用）：先跑的那个会把
 *     2 亿次空转整个跑完，才会轮到另一个 —— 结果一样"完成"，但空转次数
 *     顶到上限、时钟切换次数是 0。
 *
 * 所以判定条件写成：go_flag 立了 + 空转次数 > 0 且远小于上限 + 时钟真切换过。
 */

void sched_init(void);
unsigned long sched_switches(void);
void boot_putc(int ch);
void boot_exit(int code);
int printf(const char *fmt, ...);

#define CAP 200000000UL

static volatile int started = 0;
static volatile int go_flag = 0;
static volatile int done = 0;
static volatile unsigned long spins = 0;

static int spinner(int n)
{
    started = 1;
    while (!go_flag && spins < CAP)
        spins++;
    done++;
    return go_flag ? 1 : 0;
}

static int setter(int n)
{
    unsigned long guard = 0;

    while (!started && guard < CAP)
        guard++;
    go_flag = 1;
    done++;
    return 2;
}

void _start(void)
{
    unsigned long sw = 0;
    unsigned long sp = 0;
    int ok = 0;

    sched_init();
    spawn spinner(1);
    spawn setter(2);
    while (done < 2)
        ;                               /* 主任务也空转，等时钟切换 */
    sp = spins;
    sw = sched_switches();
    ok = go_flag && sp > 0 && sp < CAP && sw > 0;
    printf("spawn: 空转 %lu 次（上限 %lu）、时钟切换 %lu 次、go_flag=%d\n",
           sp, (unsigned long)CAP, sw, go_flag);
    if (ok)
        printf("spawn: 是并发 —— 两个任务在同一个 CPU 上被时钟抢占着交错推进\n");
    else
        printf("spawn: 不是并发\n");
    boot_exit(ok ? 0 : 3);
}
