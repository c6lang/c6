/* 裸机上的抢占式调度器 + 那 8 个 pthread 入口 —— 这就是"内核那一层"的
 * 最小版本，有了它 go / thread 在裸机上就是**真并发**：
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
 * 五个 qword（不是只在特权级变化时才弹），而 CPU 在同一特权级中断时只压
 * 前三个 —— 所以帧必须自己补齐；另外 boot.S 里 0x18 才是 64 位代码段，
 * 中断门的 selector 写 0x08 会在第一次时钟中断就 #GP(0x08)。
 *
 * 单核上是真并发（交错推进），不是并行；并行要多核，见 smp.plain.c。
 *
 * 接口就 8 个：pthread_create / join / once / key_create / getspecific /
 * setspecific / self / equal。libtcc1 的 go 只认这几个符号，别的都不问。
 */

#define MAXTASK 32
#define STACKSZ (64 * 1024)
#define KEYSLOTS 8
#define TICK_HZ 100
#define ISTACKSZ 8192

/* 被打断的现场。字段顺序 = 中断入口压栈顺序（最后压的在最低地址），
   所以整块 160 字节可以直接搬进搬出。 */
struct Frame {
    unsigned long long rax, rbx, rcx, rdx, rsi, rdi, rbp;
    unsigned long long r8, r9, r10, r11, r12, r13, r14, r15;
    unsigned long long rip, cs, fl, rsp, ss;
};

struct Task {
    struct Frame f;                     /* 现场 */
    unsigned long long sp;              /* 老版本字段，留着方便打印 */
    char *stack;                        /* 栈底（calloc 的整块） */
    int (*fn)(void *);
    void *arg;
    void *ret;
    volatile int done;                  /* fn 已经返回 */
    volatile int dead;                  /* 别再调度它了 */
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
void sched_inb(int port);
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
    "  ret\n");

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

/* 中断里调用：把现场存进当前任务，挑下一个，返回它的现场。 */
struct Frame *sched_switch(struct Frame *f)
{
    struct Task *t = 0;
    int i = 0, n = 0, start = 0;

    sched_outb(0x20, 0x20);             /* EOI */
    if (cur) {
        cur->f = *f;
        cur->sp = (unsigned long long)(unsigned long)f;
    }
    switch_count++;
    n = ntasks;
    for (i = 0; i < n; i++)
        if (tasks[i] == cur) {
            start = i;
            break;
        }
    for (i = 1; i <= n; i++) {
        struct Task *c = tasks[(start + i) % n];
        if (!c->dead) {
            t = c;
            break;
        }
    }
    if (!t)
        t = cur;
    cur = t;
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

int sched_core(void)
{
    return 0;                           /* 单核版本 */
}

static void task_entry(void)
{
    struct Task *t = cur;

    t->ret = (void *)(long)t->fn(t->arg);
    t->done = 1;
    t->dead = 1;
    for (;;)
        sched_yield();                  /* 自己让出去，之后不会再被选中 */
}

/* 定时器中断入口。
 *
 * 一件事要说清楚：长模式下的 iretq **总是**弹 rip/cs/rflags/rsp/ss 五个
 * qword，但同一特权级的中断 CPU 只压了前三个。所以这里立刻换到中断栈，
 * 把五个 qword 补齐摆好，再压 15 个通用寄存器 —— 摆出来的布局正好等于
 * struct Frame，于是"保存现场"和"恢复现场"都只是搬 160 字节。 */
__asm__(
    ".text\n"
    ".globl sched_irq0\n"
    "sched_irq0:\n"
    "  movq %rax, sched_sc_rax(%rip)\n"
    "  movq %rcx, sched_sc_rcx(%rip)\n"
    "  movq %rsp, sched_sc_old(%rip)\n"      /* 被打断处的栈：里面是 CPU 的三个 qword */
    "  movq istack_top(%rip), %rsp\n"        /* 换到中断栈，任务栈从此不再碰 */
    "  movq sched_sc_old(%rip), %rax\n"
    "  pushq $0x10\n"                        /* ss */
    "  leaq 24(%rax), %rcx\n"
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

    tasks[ntasks++] = t;
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
