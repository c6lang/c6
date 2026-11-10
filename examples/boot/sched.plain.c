/* 裸机上的抢占式调度器 + 那 8 个 pthread 入口 —— 这就是"内核那一层"的
 * 最小版本，有了它 spawn / thread 在裸机上就是**真并发**：
 *
 *   - 8259A 重映射到 0x20..0x2F，8254 PIT 通道 0 每 10ms 中断一次；
 *   - 中断进来立刻换到调度器自己的中断栈，把被打断的现场（15 个通用寄存器
 *     + rip/cs/rflags/rsp/ss）摆成一块固定大小的 struct Frame；
 *   - 每个任务保存的就是这么一块 Frame，换任务 = 换一块 Frame；
 *   - 新任务的 Frame 是编出来的："从没运行过"的样子，iretq 直接跳进
 *     task_entry，用的是它自己那块 64KB 栈；
 *   - 轮转调度，任务结束把自己的 dead 置上，下一轮就不再被选中。
 *
 * 实测踩到的坑记在这儿：长模式下的 iretq **总是**弹 rip/cs/rflags/rsp/ss
 * 五个 qword，而中断进来时 CPU 压了几个，是分机器的：这台（KVM，同一特权
 * 级）压五个，且被打断的 rsp 就记在 [入口 rsp + 24] 那格 —— 所以要读它，
 * 不要自己算"入口 rsp + 24"。算错的后果极其隐蔽：被打断的函数带着偏低
 * 16 字节的 rsp 继续跑，rbp 寻址的代码全都自己修回来了，只有 ret 弹到栈上
 * 才炸成 `#UD at 0xffff..`，看着像随机的三重故障（`fstest-bare-smp` 之前
 * 几跑一偶发就是这个）。另外 boot.S 里 0x18 才是 64 位代码段，中断门的
 * selector 写 0x08 会在第一次时钟中断就 #GP(0x08)。
 *
 * 单核上是真并发（交错推进），不是并行；并行要多核，见 smp.plain.c。
 *
 * 接口就 8 个：pthread_create / join / once / key_create / getspecific /
 * setspecific / self / equal。libtcc1 的 spawn 只认这几个符号，别的都不问。
 */

#define MAXTASK 32
#define MAXCORE 4
#define APSTACKSZ (32 * 1024)
#define STACKSZ (64 * 1024)
#define KEYSLOTS 8
#define TICK_HZ 100
#ifndef ISTACKSZ
#define ISTACKSZ 8192
#endif

/* 被打断的现场。字段顺序 = 中断入口压栈顺序（最后压的在最低地址），
   所以整块 160 字节可以直接搬进搬出。 */
struct Frame {
    unsigned long long rax, rbx, rcx, rdx, rsi, rdi, rbp;
    unsigned long long r8, r9, r10, r11, r12, r13, r14, r15;
    unsigned long long rip, cs, fl, rsp, ss;
};

#define T_READY 0
#define T_RUNNING 1
#define T_DONE 2

struct Task {
    struct Frame f;                     /* 现场 */
    unsigned long long sp;              /* 老版本字段，留着方便打印 */
    char *stack;                        /* 栈底（calloc 的整块） */
    int (*fn)(void *);
    void *arg;
    void *ret;
    volatile int done;                  /* fn 已经返回 */
    volatile int dead;                  /* 别再调度它了 */
    volatile int state;                 /* T_READY / T_RUNNING / T_DONE */
    int core;                           /* 最后一次跑在哪个核 */
    int ap_only;                        /* 只交给 AP 跑（有多核时）*/
    int tid;
    void *keys[KEYSLOTS];
};

static struct Task *tasks[MAXTASK];
static int ntasks = 0;
static struct Task *cur = 0;
static struct Task main_task;
static volatile unsigned long switch_count = 0;
static int next_key = 0;
static int main_started = 0;
static volatile int sched_lock = 0;
static volatile int ap_ready[MAXCORE];
static unsigned char ap_stack_mem[MAXCORE][APSTACKSZ];

/* 中断栈：中断一进来就换到这块，任务栈上不留任何东西。 */
static unsigned char istack[ISTACKSZ];
static unsigned long long istack_top = 0;

/* 中断入口的临时变量（单核一份；多核时按核各一份）。 */
unsigned long long sched_sc_rax = 0;
unsigned long long sched_sc_rcx = 0;
unsigned long long sched_sc_old = 0;

void *calloc(unsigned long n, unsigned long sz);

/* ---- 几条汇编小工具（tcc 的约束支持有限，干脆写成全局 asm） ---- */

void sched_outb(int port, int val);
int sched_inb(int port);
void sched_cli(void);
void sched_sti(void);
void sched_idle(void);                  /* sti; hlt */
void sched_lidt(void *p);
void sched_int20(void);                 /* int $0x20：主动让出 */
void sched_pause(void);                 /* pause：自旋时别把流水线烧穿 */
int sched_xchg(int *p, int v);

__asm__(
    ".text\n"
    ".globl sched_outb\n"
    "sched_outb:\n"                     /* (int port, int val) */
    "  movl %esi, %eax\n"
    "  movl %edi, %edx\n"
    "  outb %al, %dx\n"
    "  ret\n"
    ".globl sched_inb\n"
    "sched_inb:\n"
    "  movl %edi, %edx\n"
    "  xorl %eax, %eax\n"
    "  inb %dx, %al\n"
    "  ret\n"
    ".globl sched_cli\n"
    "sched_cli:\n  cli\n  ret\n"
    ".globl sched_sti\n"
    "sched_sti:\n  sti\n  ret\n"
    ".globl sched_idle\n"
    "sched_idle:\n  sti\n  hlt\n  ret\n"
    ".globl sched_pause\n"
    "sched_pause:\n  pause\n  ret\n"
    ".globl sched_lidt\n"
    "sched_lidt:\n  lidt (%rdi)\n  ret\n"
    ".globl sched_int20\n"
    "sched_int20:\n  int $0x20\n  ret\n"
    ".globl sched_xchg\n"
    "sched_xchg:\n"
    "  movl %esi, %eax\n"
    "  xchgl %eax, (%rdi)\n"
    "  ret\n"
    ".globl sched_read_cr3\n"
    "sched_read_cr3:\n  movq %cr3, %rax\n  ret\n");

/* ---- 中断门 ---- */

struct Idte {
    unsigned short off_lo;
    unsigned short sel;
    unsigned char ist;
    unsigned char type;
    unsigned short off_mid;
    unsigned int off_hi;
    unsigned int zero;
};

struct Idtr {
    unsigned short limit;
    unsigned long long base;
} __attribute__((packed));

static struct Idte idt[256];
static struct Idtr idtr;

static void set_gate(int vec, void (*fn)(void))
{
    unsigned long long a = (unsigned long long)(void *)fn;

    idt[vec].off_lo = (unsigned short)(a & 0xFFFF);
    idt[vec].sel = 0x18;                /* boot.S 里 0x18 才是 64 位代码段 */
    idt[vec].ist = 0;
    idt[vec].type = 0x8E;               /* 存在 | DPL0 | 64 位中断门 */
    idt[vec].off_mid = (unsigned short)((a >> 16) & 0xFFFF);
    idt[vec].off_hi = (unsigned int)(a >> 32);
    idt[vec].zero = 0;
}

void boot_putc(int ch);
void boot_exit(int code);
void sched_irq0(void);                  /* 定时器中断入口（下面用全局 asm 写） */

static void trap_handler(void)
{
    /* 异常来了：打一个记号就停机，比三重故障好查 */
    boot_putc('!');
    boot_exit(3);
    for (;;)
        ;
}

/* ---- 调度 ---- */

struct Frame *sched_switch(struct Frame *f);
static void task_entry(void);
int sched_cpu_count(void);
void ap_main(unsigned long apic_id);

/* 中断里调用：把现场存进当前任务，挑下一个，返回它的现场。 */
struct Frame *sched_switch(struct Frame *f)
{
    struct Task *t = 0;
    int i = 0, n = 0, start = 0;

    sched_outb(0x20, 0x20);             /* EOI */
    while (sched_xchg((int *)&sched_lock, 1))
        ;                               /* 任务表可能正被某个 AP 翻着 */
    if (cur) {
        cur->f = *f;
        cur->sp = (unsigned long long)(unsigned long)f;
    }
    switch_count++;
    if (cur && cur->state == T_RUNNING)
        cur->state = T_READY;
    n = ntasks;
    for (i = 0; i < n; i++)
        if (tasks[i] == cur) {
            start = i;
            break;
        }
    for (i = 1; i <= n; i++) {
        struct Task *c = tasks[(start + i) % n];
        /* 正在别的核上跑的任务不能抢：否则它会同时在两个核上执行 */
        /* 正在别的核上跑的任务不能抢：否则它会同时在两个核上执行 */
        if (c->dead || c->state == T_RUNNING)
            continue;
        /* 有多核时，标了 ap_only 的活留给 AP */
        if (c->ap_only && sched_cpu_count() > 1)
            continue;
        t = c;
        break;
    }
    if (!t)
        t = cur;
    cur = t;
    cur->state = T_RUNNING;
    sched_xchg((int *)&sched_lock, 0);
    return &cur->f;
}

void sched_yield(void)
{
    sched_int20();
}

unsigned long sched_switches(void)
{
    return switch_count;
}

struct Task *sched_self(void)
{
    return cur;
}

static void task_entry(void)
{
    struct Task *t = cur;

    t->ret = (void *)(long)t->fn(t->arg);
    t->done = 1;
    t->dead = 1;
    t->state = T_DONE;
    for (;;)
        sched_yield();                  /* 自己让出去，之后不会再被选中 */
}

/* 定时器中断入口。
 *
 * 一件事要说清楚：长模式下的 iretq **总是**弹 rip/cs/rflags/rsp/ss 五个
 * qword，而中断进来时 CPU 压了几个是分机器的。实测这台机器（KVM，同一
 * 特权级）：压五个，而且被打断的 rsp 就明明白白记在 [入口 rsp + 24] 里
 * ——所以**不要自己算**"入口 rsp + 24"，直接读这一格。自己算的下场是偏低
 * 16 字节：被打断的函数带着错的 rsp 继续跑，rbp 寻址的代码全都自己修回来
 * 了，只有 ret 弹到栈上才炸成 `#UD at 0xffff..`，看着像随机三重故障
 * （`fstest-bare-smp` 之前几跑一偶发就是这个）。只压 rip/cs/rflags 三个的
 * 实现也留着兜底。摆好五个 qword 再压 15 个通用寄存器 —— 布局正好等于
 * struct Frame，于是"保存现场"和"恢复现场"都只是搬 160 字节。 */
__asm__(
    ".text\n"
    ".globl sched_irq0\n"
    "sched_irq0:\n"
    "  movq %rax, sched_sc_rax(%rip)\n"
    "  movq %rcx, sched_sc_rcx(%rip)\n"
    "  movq %rsp, sched_sc_old(%rip)\n"      /* 被打断处的栈：里面是 CPU 压的中断帧 */
    "  movq istack_top(%rip), %rsp\n"        /* 换到中断栈，任务栈从此不再碰 */
    "  movq sched_sc_old(%rip), %rax\n"
    "  leaq 24(%rax), %rcx\n"                /* 兜底：CPU 只压了 rip/cs/rflags 才用它 */
    "  cmpq $0x10, 32(%rax)\n"               /* CPU 把 ss 也压了吗？（0x10 是我们的数据段） */
    "  jne sched_irq0_have_rsp\n"
    "  movq 24(%rax), %rcx\n"                /* 压了：被打断的 rsp 就记在这一格里 */
    "sched_irq0_have_rsp:\n"
    "  pushq $0x10\n"                        /* ss */
    "  pushq %rcx\n"                         /* rsp（= 被打断时的 rsp） */
    "  movq 16(%rax), %rcx\n"
    "  pushq %rcx\n"                         /* rflags */
    "  movq 8(%rax), %rcx\n"
    "  pushq %rcx\n"                         /* cs */
    "  movq (%rax), %rcx\n"
    "  pushq %rcx\n"                         /* rip */
    "  pushq %r15\n  pushq %r14\n  pushq %r13\n  pushq %r12\n"
    "  pushq %r11\n  pushq %r10\n  pushq %r9\n  pushq %r8\n"
    "  pushq %rbp\n  pushq %rdi\n  pushq %rsi\n  pushq %rdx\n"
    "  pushq sched_sc_rcx(%rip)\n"
    "  pushq %rbx\n"
    "  pushq sched_sc_rax(%rip)\n"           /* 最后压 rax → 它排在结构体最低处 */
    "  movq %rsp, %rdi\n"
    "  call sched_switch\n"                  /* -> 下一个任务的 Frame* */
    "  movq istack_top(%rip), %rsp\n"
    "  subq $160, %rsp\n"
    "  movq %rax, %rsi\n"
    "  movq %rsp, %rdi\n"
    "  movq $20, %rcx\n"
    "  cld\n"
    "  rep movsq\n"
    "  popq %rax\n  popq %rbx\n  popq %rcx\n  popq %rdx\n"
    "  popq %rsi\n  popq %rdi\n  popq %rbp\n  popq %r8\n"
    "  popq %r9\n  popq %r10\n  popq %r11\n  popq %r12\n"
    "  popq %r13\n  popq %r14\n  popq %r15\n"
    "  iretq\n");

static void pit_init(void)
{
    unsigned int div = 1193182u / TICK_HZ;

    sched_outb(0x43, 0x36);             /* 通道 0，先低后高，方式 3 */
    sched_outb(0x40, (int)(div & 0xFF));
    sched_outb(0x40, (int)((div >> 8) & 0xFF));
}

static void pic_init(void)
{
    sched_cli();
    sched_outb(0x20, 0x11);             /* ICW1：级联、要 ICW4 */
    sched_outb(0xA0, 0x11);
    sched_outb(0x21, 0x20);             /* 主片向量基址 0x20 */
    sched_outb(0xA1, 0x28);             /* 从片向量基址 0x28 */
    sched_outb(0x21, 0x04);
    sched_outb(0xA1, 0x02);
    sched_outb(0x21, 0x01);
    sched_outb(0xA1, 0x01);
    sched_outb(0x21, 0xFE);             /* 只开 IRQ0（定时器） */
    sched_outb(0xA1, 0xFF);
}

void sched_init(void)
{
    int i = 0;

    istack_top = (unsigned long long)(unsigned long)(istack + ISTACKSZ);
    istack_top &= ~15ULL;

    for (i = 0; i < 256; i++)
        set_gate(i, trap_handler);
    set_gate(0x20, sched_irq0);

    idtr.limit = (unsigned short)(sizeof idt - 1);
    idtr.base = (unsigned long long)(unsigned long)idt;
    sched_lidt(&idtr);

    main_task.tid = 0;
    main_task.done = 0;
    main_task.dead = 0;
    main_task.state = T_RUNNING;
    main_task.ap_only = 0;
    main_task.core = 0;
    main_task.fn = 0;
    main_task.sp = 0;
    for (i = 0; i < KEYSLOTS; i++)
        main_task.keys[i] = 0;
    for (i = 0; i < MAXTASK; i++)
        tasks[i] = 0;
    tasks[0] = &main_task;
    ntasks = 1;
    cur = &main_task;
    main_started = 1;

    pic_init();
    pit_init();
    sched_sti();
}


/* ---- 多核：把 AP 拉起来，让它从任务表里捡活干 ------------------------ */

#define LAPIC_BASE 0xFEE00000UL

static unsigned int lapic_rd(int off)
{
    return *(volatile unsigned int *)(LAPIC_BASE + (unsigned long)off);
}

static void lapic_wr(int off, unsigned int v)
{
    *(volatile unsigned int *)(LAPIC_BASE + (unsigned long)off) = v;
}

int sched_core(void)
{
    return (int)((lapic_rd(0x20) >> 24) & (MAXCORE - 1));
}

static void lapic_deliver_wait(void)
{
    int i = 0;

    while ((lapic_rd(0x300) & (1u << 12)) && i < 1000000)
        i++;
}

/* 延时：PIT 通道 2 的单次触发（通道 0 还在给调度器打拍子，别动它）。 */
static void delay_10ms(void)
{
    unsigned int div = 11932u;
    int i = 0;


    sched_outb(0x61, (sched_inb(0x61) & 0xFD) | 1);   /* 开 gate2、关扬声器 */
    sched_outb(0x43, 0xB2);                            /* 通道 2、方式 1 */
    sched_outb(0x42, (int)(div & 0xFF));
    sched_outb(0x42, (int)((div >> 8) & 0xFF));
    while (!(sched_inb(0x61) & 0x20) && i < 10000000)
        i++;
}

static void lapic_send_init(int id)
{
    lapic_wr(0x310, (unsigned int)id << 24);
    lapic_wr(0x300, 0x000C4500);        /* INIT，assert */
    lapic_deliver_wait();
}

static void lapic_send_sipi(int id, int vec)
{
    lapic_wr(0x310, (unsigned int)id << 24);
    lapic_wr(0x300, 0x000C4600u | (unsigned int)(vec & 0xFF));
    lapic_deliver_wait();
}

/* 跳板由 build.sh 用 objcopy -I binary 包成目标文件链进来。 */
extern const unsigned char _binary_ap_bin_start[];
extern const unsigned char _binary_ap_bin_end[];

static unsigned long long ap_stacks[16];

int sched_cpu_count(void)
{
    int i = 0, n = 1;

    for (i = 0; i < MAXCORE; i++)
        if (ap_ready[i])
            n++;
    return n;
}

/* 三个约定好的低内存邮箱（ap.S 里按绝对地址读）：
 *   0x7000 u32 cr3 / 0x7008 u64 ap_entry / 0x7010 u64 ap_stacks[16]
 */
int smp_start_aps(int ncpu)
{
    int bsp = sched_core();
    int id = 0, i = 0;
    unsigned long n = (unsigned long)(_binary_ap_bin_end - _binary_ap_bin_start);

    sched_cli();                        /* 启动 AP 期间先别让时钟插进来 */
    for (i = 0; i < 16; i++) {
        unsigned long long top = (unsigned long long)(unsigned long)
            (ap_stack_mem[i & (MAXCORE - 1)] + APSTACKSZ);
        ap_stacks[i] = top & ~15ULL;
    }
    memcpy((void *)0x8000, _binary_ap_bin_start, n);
    *(volatile unsigned int *)0x7000 = (unsigned int)sched_read_cr3();
    *(volatile unsigned long long *)0x7008 =
        (unsigned long long)(unsigned long)ap_main;
    *(volatile unsigned long long *)0x7010 =
        (unsigned long long)(unsigned long)ap_stacks;
    if (ncpu > MAXCORE)
        ncpu = MAXCORE;
    for (id = 0; id < ncpu; id++) {
        if (id == 0 || id == bsp)
            continue;
        lapic_send_init(id);
        delay_10ms();
        lapic_send_sipi(id, 0x08);      /* 0x08 → 0x8000 */
        delay_10ms();
        lapic_send_sipi(id, 0x08);
        delay_10ms();
    }
    for (i = 0; i < 200; i++) {
        if (sched_cpu_count() >= ncpu)
            break;
        delay_10ms();
    }
    sched_sti();
    return sched_cpu_count();
}

/* AP 从跳板跳到这里。它不装 IDT、不开时钟，就是一直从任务表里捡活干：
 * 捡到一个就直接把函数调起来跑完（那套编出来的 iretq 现场是给引导核用的）。 */
void ap_main(unsigned long apic_id)
{
    int core = (int)(apic_id & (MAXCORE - 1));

    if (core == 0)
        return;                         /* 不该走到这：引导核已经起来了 */
    ap_ready[core] = 1;
    for (;;) {
        struct Task *t = 0;
        int i = 0, n = 0;

        sched_cli();
        while (sched_xchg((int *)&sched_lock, 1))
            ;
        n = ntasks;
        for (i = 0; i < n; i++) {
            struct Task *c = tasks[i];
            /* 引导核自己的上下文不能搬到别的核上跑 */
            if (c && c != &main_task && !c->dead && c->state == T_READY) {
                c->state = T_RUNNING;
                c->core = core;
                t = c;
                break;
            }
        }
        sched_xchg((int *)&sched_lock, 0);
        /* AP 上不装 IDT，所以这里绝不 sched_sti()：开了中断就是三重故障 */
        if (!t) {
            sched_pause();
            continue;
        }
        t->ret = (void *)(long)t->fn(t->arg);
        t->done = 1;
        t->dead = 1;
        t->state = T_DONE;
    }
}

/* ---- 那 8 个入口 ---- */

int pthread_create(unsigned long *th, const void *attr,
                   void *(*fn)(void *), void *arg)
{
    struct Task *t = 0;
    char *top = 0;
    int i = 0;

    (void)attr;
    if (!main_started)
        sched_init();
    if (ntasks >= MAXTASK || !fn)
        return -1;
    t = (struct Task *)calloc(1, sizeof *t);
    if (!t)
        return -1;
    t->stack = (char *)calloc(1, STACKSZ);
    if (!t->stack)
        return -1;
    t->fn = fn;
    t->arg = arg;
    t->ret = 0;
    t->done = 0;
    t->dead = 0;
    t->state = T_READY;
    t->core = -1;
    t->ap_only = (sched_cpu_count() > 1);
    t->tid = ntasks;
    for (i = 0; i < KEYSLOTS; i++)
        t->keys[i] = 0;

    /* 编一块"从没运行过"的现场：寄存器全 0，iretq 落进 task_entry，
       栈从自己那块 64KB 的顶上一格开始（16 字节对齐、ABI 也顺） */
    top = t->stack + STACKSZ;
    for (i = 0; i < 15; i++)
        ((unsigned long long *)(void *)&t->f)[i] = 0;
    t->f.rip = (unsigned long long)(unsigned long)task_entry;
    t->f.cs = 0x18;                     /* 64 位代码段（boot.S 里 0x18） */
    t->f.fl = 0x202;                    /* IF=1 */
    t->f.rsp = (unsigned long long)(unsigned long)(top - 8);
    t->f.ss = 0x10;                     /* 数据段 */
    t->sp = 0;

    sched_cli();                        /* 别让中断或别的核看见半成品 */
    tasks[ntasks++] = t;
    sched_sti();
    if (th)
        *th = (unsigned long)t;
    return 0;
}

int pthread_join(unsigned long th, void **ret)
{
    struct Task *t = (struct Task *)(void *)th;

    if (!t)
        return -1;
    while (!t->done)
        sched_pause();                  /* 自旋等下一次时钟中断（见文末"hlt 的坑"） */
    if (ret)
        *ret = t->ret;
    return 0;
}

int pthread_once(int *once, void (*fn)(void))
{
    if (!once || !fn)
        return -1;
    if (sched_xchg(once, 1) == 0)
        fn();
    return 0;
}

int pthread_key_create(int *key, void (*dtor)(void *))
{
    (void)dtor;
    if (!key || next_key >= KEYSLOTS)
        return -1;
    *key = next_key++;
    return 0;
}

void *pthread_getspecific(int key)
{
    if (!cur || key < 0 || key >= KEYSLOTS)
        return 0;
    return cur->keys[key];
}

int pthread_setspecific(int key, const void *val)
{
    if (!cur || key < 0 || key >= KEYSLOTS)
        return -1;
    cur->keys[key] = (void *)val;
    return 0;
}

unsigned long pthread_self(void)
{
    return (unsigned long)cur;
}

int pthread_equal(unsigned long a, unsigned long b)
{
    return a == b;
}
