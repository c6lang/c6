/* 裸机上的 c6 程序：没有内核、没有 libc、没有 syscall
 *
 *   make fstest-bare
 *
 * 由 examples/boot/boot.S 里的最小引导器把机器带进长模式后调用 _start()。
 * 程序能用的只有自己写的 boot_putc / boot_exit（两个都只是 outb 指令），以及
 * libtcc1 里的 c6 运行时——它的外部依赖全是 weak 的，这里一个都不提供：
 * 分配器用 libtcc1 自带的静态 arena，mem* 用自带的字节循环版，线程退化成
 * 单线程，select 只有 async 才碰。
 *
 * 输出走 qemu 的 0xE9 调试口（-debugcon stdio），结束走 isa-debug-exit：
 * qemu 的退出码是 (code << 1) | 1，所以 code=0 时退出码是 1，就是“通过”。
 */
struct Pair {
    int v;
};

void boot_putc(int ch);
void boot_exit(int code);

/* 两个“平台”动作：qemu 的 0xE9 调试口和 isa-debug-exit。
   就这么两条 outb，别的什么都没有。 */
__asm__(
    ".text\n"
    ".globl boot_putc\n"
    "boot_putc:\n"
    "  movl %edi, %eax\n"
    "  movl $0xE9, %edx\n"
    "  outb %al, %dx\n"
    "  ret\n"
    ".globl boot_exit\n"
    "boot_exit:\n"
    "  movl %edi, %eax\n"
    "  movl $0x501, %edx\n"
    "  outb %al, %dx\n"
    "1: hlt\n"
    "  jmp 1b\n");

static void puts_(const char *s)
{
    while (*s)
        boot_putc(*s++);
}

static int eq(const char *a, const char *b)
{
    unsigned long i = 0;

    for (i = 0; a[i] || b[i]; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

int!error half(int n)
{
    if (n < 0)
        err(9, f"negative {n}");
    return n / 2;
}

void _start(void)
{
    int bad = 0, n = 42, got = 0;
    double d = 3.14159265;
    char[:] s = "bare";
    @Pair p = new Pair;
    @char man = 0;

    /* defer 收尾：打印结果 + 关掉 qemu（退出码由 bad 决定） */
    defer {
        puts_(bad ? "bare metal: FAILED\n" : "bare metal: ok\n");
        boot_exit(bad ? 1 : 0);
    }

    p->v = n;
    man = @f"<{n}>";
    puts_(f"n={n} hex={n;04x} pi={d;.3f}\n");
    puts_(f"s={s} p={p->v} managed={man}\n");
    bad |= eq(f"{n;5}", "   42") ? 0 : 1;          /* 宽度 */
    bad |= eq(f"{d;.2e}", "3.14e+00") ? 0 : 2;     /* 浮点 */
    bad |= eq(@f"{n}:{s}", "42:bare") ? 0 : 4;     /* 托管缓冲 */
    bad |= eq(f"{p->v;3}", " 42") ? 0 : 8;         /* 托管对象成员 */

    {
        int h = half(10) catch e { bad |= 16; goto no_err; };
        bad |= h == 5 ? 0 : 16;                    /* 没出错 */
    no_err:
        ;
    }
    {
        int v = half(-3) catch e { got = e.id; goto caught; };
        bad |= 32;                                 /* 不该走到这里 */
    caught:
        bad |= got == 9 ? 0 : 64;
        (void)v;
    }
    return;                                        /* 让上面的 defer 收尾 */
}
