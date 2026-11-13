/* 最小引导器：512 字节的 BIOS 引导扇区，把 c6 程序从磁盘读进来、切成
 * 长模式、跳到它那里。中间没有内核、没有 libc、没有 syscall。
 *
 *   cmake --build build --target fstest-bare        （见 examples/boot/build.sh）
 *
 * 做的事按顺序：
 *   1. 实模式：打开 A20、装 GDT，进 32 位保护模式；
 *   2. 保护模式：用 ATA PIO 把 payload 从 LBA 1 **直接读进链接地址
 *      0x400000**。不用 INT 13h 是因为它的目标地址是「段:偏移」，上限约
 *      1 MiB，而大 payload（比如 tests/data_zero.c6 有 855 KiB）既超过这个
 *      上限、也不能先落在 0x10000——0x10000 + 855 KiB 会压到 0xA0000 起的
 *      VGA/ROM 空洞，写进去读出来是 0xFF；
 *   3. 在 0x90000 建三个页表，把第一个 1GB 恒等映射到 0；
 *   4. 开 PAE / LME / 分页，切到 64 位；
 *   5. 清零 payload 的 .bss；
 *   6. call 进去。
 *
 * 栈在保护模式用 0x70000、长模式用 0x1000000，页表在 0x90000，都还没被别的
 * 东西占着；payload 自己占 0x400000 往上。
 *
 * 这些常量由构建脚本用 --defsym 传进来：
 *   payload_sectors  要从磁盘读多少个 512 字节扇区
 *   payload_bytes    实际字节数（拷贝用）
 *   payload_bss      .bss 字节数（拷贝完清零）
 *   payload_entry    c6 程序入口的线性地址（从 nm 取）
 */

        .code16
        .section .boot, "ax"
        .globl _start
_start:
        cli
        xorw %ax, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw $0x7C00, %sp
        sti

        /* --- A20（qemu 一般已经开了，顺手写一次） --- */
        inb $0x92, %al
        orb $2, %al
        outb %al, $0x92

        /* --- 进 32 位保护模式 --- */
        cli
        lgdt gdt_desc
        movl %cr0, %eax
        orl $1, %eax
        movl %eax, %cr0
        ljmpl $0x08, $pm_entry

/* ---- 32 位保护模式 -------------------------------------------------- */

        .code32
pm_entry:
        movw $0x10, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs
        movl $0x70000, %esp             /* 保护模式的栈（页表在 0x90000） */
        /* --- 用 ATA PIO 把 payload 直接读进 0x400000 --- */
        movl $0x400000, %edi
        movl $1, %ebx                   /* 起始 LBA */
        movl $payload_sectors, %ecx
        call ata_read
        testl %eax, %eax
        jnz read_fail32
        /* 页表：PML4[0] -> PDPT，PDPT[0] -> PD，PD = 512 个 2MB 页 */
        movl $0x90000, %edi             /* PML4 */
        xorl %eax, %eax
        movl $(4 * 4096 / 4), %ecx
        rep stosl
        movl $0x91003, 0x90000          /* PML4[0] = PDPT | 3 */
        movl $0x92003, 0x91000          /* PDPT[0] = PD   | 3 */
        movl $0x93003, 0x91018          /* PDPT[3] = 高 PD | 3（3~4GB）*/
        movl $0x92000, %ebx
        xorl %ecx, %ecx
1:      movl %ecx, %eax
        shll $21, %eax
        orl $0x83, %eax
        movl %eax, (%ebx,%ecx,8)
        incl %ecx
        cmpl $512, %ecx
        jb 1b
        /* LAPIC 在 0xFEE00000（3~4GB 那一段），单独映射一页 2MB：
           PDPT[3] 指的那个 PD 里第 (0xFEE00000>>21)&0x1FF = 0x1F7 项 */
        movl $0xFEE00093, 0x93FB8
        /* 开 SSE：长模式下 c6 的浮点/结构拷贝会用 xmm，CR4.OSFXSR 不开就 #UD */
        movl %cr0, %eax
        andl $0xFFFFFFF3, %eax          /* 清 EM(2) 和 TS(3) */
        orl $0x12, %eax                 /* MP(1) | ET(4) */
        movl %eax, %cr0
        movl %cr4, %eax
        orl $((1 << 9) | (1 << 10)), %eax   /* OSFXSR | OSXMMEXCPT */
        movl %eax, %cr4
        /* 开 PAE */
        movl $0x90000, %eax
        movl %eax, %cr3
        movl %cr4, %eax
        orl $(1 << 5), %eax
        movl %eax, %cr4
        /* 开 EFER.LME */
        movl $0xC0000080, %ecx
        rdmsr
        orl $(1 << 8), %eax
        wrmsr
        /* 开分页 */
        movl %cr0, %eax
        orl $((1 << 31) | 1), %eax
        movl %eax, %cr0
        ljmpl $0x18, $lm_entry

/* ---- 64 位 ---------------------------------------------------------- */

        .code64
lm_entry:
        movw $0x10, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        /* 栈放在 16MB 上：payload 的 .bss 从 0x40xxxx 起会一路长到 8MB 多，
           0x80000 这种低地址其实**落在 .bss 里面**，变量一多就会踩到栈。 */
        movq $0x01000000, %rsp
        xorq %rbp, %rbp
        /* payload 已经在 0x400000（保护模式里直接读进来的）；
           清零它的 .bss（大小从 ELF 的 memsz - filesz 来） */
        movq $0x400000, %rdi
        addq $payload_bytes, %rdi
        movl $payload_bss, %ecx
        xorl %eax, %eax
        rep stosb
        cli                             /* 交给 payload 时中断一定是关的 */
        movq $payload_entry, %rax
        call *%rax
2:      hlt
        jmp 2b

/* ---- GDT 与磁盘地址包 ----------------------------------------------- */

        .align 8
gdt:
        .quad 0
        .quad 0x00CF9A000000FFFF        /* 0x08：32 位代码 */
        .quad 0x00CF92000000FFFF        /* 0x10：数据 */
        .quad 0x00AF9A000000FFFF        /* 0x18：64 位代码 */
gdt_end:
gdt_desc:
        .word gdt_end - gdt - 1
        .long gdt

        /* ---- ATA PIO 读盘（主通道，28 位 LBA，轮询，无中断） ---------------
 *
 *   EDI = 目标线性地址（ES 已是指向 4GB 的平坦段）
 *   EBX = 起始 LBA（从 1 开始，LBA 0 是引导扇区）
 *   ECX = 扇区数
 *   返回 EAX = 0 成功 / 2 失败（超时或 ERR 置位）
 *
 * 端口：0x1F0 数据、0x1F2 计数、0x1F3..0x1F5 LBA 低 24 位、
 *       0x1F6 驱动器/头 + LBA 高 4 位、0x1F7 状态/命令。
 */
        .code32
ata_read:
        pushl %ebp
        pushl %esi
        pushl %ebx
        pushl %ecx
        pushl %edx
1:      testl %ecx, %ecx
        jz 9f
        /* 等 BSY 清 */
        movl $500000, %esi
2:      movw $0x1f7, %dx
        inb %dx, %al
        testb $0x80, %al
        jz 3f
        decl %esi
        jnz 2b
        jmp 8f
3:      /* 发 READ SECTORS (0x20)，一次一个扇区 */
        movw $0x1f2, %dx
        movb $1, %al
        outb %al, %dx
        movw $0x1f3, %dx
        movb %bl, %al
        outb %al, %dx
        movw $0x1f4, %dx
        movb %bh, %al
        outb %al, %dx
        movw $0x1f5, %dx
        movl %ebx, %eax
        shrl $16, %eax
        outb %al, %dx
        movw $0x1f6, %dx
        movl %ebx, %eax
        shrl $24, %eax
        andb $0x0f, %al
        orb $0xe0, %al
        outb %al, %dx
        movw $0x1f7, %dx
        movb $0x20, %al
        outb %al, %dx
        /* 等 DRQ（BSY 清且 DRQ 置；ERR 置位算失败） */
        movl $500000, %esi
4:      movw $0x1f7, %dx
        inb %dx, %al
        testb $0x80, %al
        jnz 5f
        testb $0x08, %al
        jnz 6f
        testb $0x01, %al
        jnz 8f
5:      decl %esi
        jnz 4b
        jmp 8f
6:      movw $0x1f0, %dx
        pushl %ecx
        movl $256, %ecx
        rep insw
        popl %ecx
        incl %ebx
        decl %ecx
        jmp 1b
8:      movl $2, %eax
        jmp 10f
9:      xorl %eax, %eax
10:     popl %edx
        popl %ecx
        popl %ebx
        popl %esi
        popl %ebp
        ret

read_fail32:
        movw $0x501, %dx                /* qemu isa-debug-exit：code=2 */
        movl $2, %eax
        outb %al, %dx
1:      hlt
        jmp 1b

/* 引导扇区必须以 0x55AA 结尾 */
        .org 510
        .word 0xAA55
