/* 最小引导器：512 字节的 BIOS 引导扇区，把 c6 程序从磁盘读进来、切成
 * 长模式、跳到它那里。中间没有内核、没有 libc、没有 syscall。
 *
 *   make fstest-bare        （见 examples/boot/build.sh）
 *
 * 做的事按顺序：
 *   1. 实模式：用 INT 13h 的扩展读（AH=42h）把 payload 分块读到 0x10000，
 *      每次最多 16 个扇区（8KB），避免跨 64KB 段边界；
 *   2. 打开 A20、装 GDT、进 32 位保护模式；
 *   3. 在 0x90000 建三个页表，把第一个 1GB 恒等映射到 0；
 *   4. 开 PAE / LME / 分页，切到 64 位；
 *   5. 把 payload 从 0x10000 拷到它的链接地址 0x400000，清零它的 .bss；
 *   6. call 进去。
 *
 * 栈在 0x80000（64 位），页表在 0x90000，都还没被别的东西占着。
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
        movb %dl, drive                 /* BIOS 给的启动盘号 */
        sti

        /* --- A20（qemu 一般已经开了，顺手写一次） --- */
        inb $0x92, %al
        orb $2, %al
        outb %al, $0x92

        /* --- 把 payload 从 LBA 1 开始读到 0x1000:0000 --- */
        movw $payload_sectors, %bx      /* 还剩多少扇区 */
        movw $0x1000, %di               /* 目标段 */
read_loop:
        testw %bx, %bx
        jz read_done
        movw %bx, %ax
        cmpw $16, %ax
        jbe 1f
        movw $16, %ax
1:      movw %ax, dap_count
        movw $0, dap_off
        movw %di, dap_seg
        movw $dap, %si
        movb drive, %dl
        movb $0x42, %ah
        int $0x13
        jc read_fail
        movzwl dap_count, %eax          /* 这一次读了多少扇区 */
        subl %eax, %ebx                 /* 不该为负，但 EBX 是 32 位好算 */
        shll $5, %eax                   /* 每扇区 512 字节 = 32 个段 */
        addw %ax, %di
        movzwl dap_count, %eax
        addl %eax, dap_lba
        jmp read_loop

read_fail:
        movl $0x501, %edx               /* qemu isa-debug-exit：(code<<1)|1 */
        movl $2, %eax
        outb %al, %dx
1:      hlt
        jmp 1b

read_done:
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
        /* 把 payload 从 0x10000 拷到 0x400000 */
        movq $0x10000, %rsi
        movq $0x400000, %rdi
        movl $payload_bytes, %ecx
        rep movsb
        /* 清零 payload 的 .bss（大小从 ELF 的 memsz - filesz 来） */
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

        .align 4
dap:
        .byte 0x10, 0
dap_count:
        .word 0
dap_off:
        .word 0
dap_seg:
        .word 0
dap_lba:
        .quad 1
drive:
        .byte 0

/* 引导扇区必须以 0x55AA 结尾 */
        .org 510
        .word 0xAA55
