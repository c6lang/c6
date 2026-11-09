/* AP（application processor）启动跳板。
 *
 * qemu 的 -smp N 只会让 BSP 跑起来，别的核都在 INIT 状态里等着：BSP 得发
 * INIT-SIPI-SIPI，AP 会从 SIPI 向量给的**实模式**地址开始执行。所以这段代码
 * 必须从 16 位开始，一路走过 32 位保护模式再进长模式 —— tcc 的汇编器不认
 * .code16/.code32，所以这个文件走 binutils 的 as --32（和 boot.S 一样）。
 *
 * 用法（见 build.sh）：as --32 → ld -Ttext 0x8000 → 扁平的 ap.bin；BSP 把它
 * memcpy 到物理地址 0x8000，再给每个 AP 发 SIPI（向量 0x08 = 0x8000>>12）。
 *
 * 和 BSP 的约定：低内存里的三个"邮箱"，BSP 先填好再发 INIT：
 *   0x7000  u32  cr3（引导器在 0x90000 建好的页表，前 1GB 恒等映射）
 *   0x7008  u64  ap_entry（C 函数，签名叫 void ap_main(unsigned long apic_id)）
 *   0x7010  u64  ap_stacks（u64[16]，按 APIC ID 索引的栈顶）
 *
 * 段选择子和 boot.S 保持一致：0x08 = 32 位代码，0x10 = 数据，0x18 = 64 位代码。
 */

        .code16
        .section .text
        .globl _ap_start
_ap_start:
        cli
        xorw %ax, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw $0x7C00, %sp               /* 临时栈，实模式随便找个能用的 */
        lgdt ap_gdt_desc
        movl %cr0, %eax
        orl $1, %eax                    /* PE */
        movl %eax, %cr0
        ljmpl $0x08, $ap_pm32

/* ---- 32 位保护模式 -------------------------------------------------- */

        .code32
ap_pm32:
        movw $0x10, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs
        /* 开 SSE：长模式下 c6 的浮点/结构拷贝会用 xmm，CR4.OSFXSR 不开就 #UD */
        movl %cr0, %eax
        andl $0xFFFFFFF3, %eax          /* 清 EM(2) 和 TS(3) */
        orl $0x12, %eax                 /* MP(1) | ET(4) */
        movl %eax, %cr0
        movl %cr4, %eax
        orl $((1 << 9) | (1 << 10)), %eax   /* OSFXSR | OSXMMEXCPT */
        movl %eax, %cr4
        /* 用 BSP 建好的页表 */
        movl 0x7000, %eax
        movl %eax, %cr3
        /* 开 PAE */
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
        ljmpl $0x18, $ap_lm64

/* ---- 64 位 ---------------------------------------------------------- */

        .code64
ap_lm64:
        movw $0x10, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        movw %ax, %fs
        movw %ax, %gs
        /* 自己的 APIC ID：LAPIC 的 ID 寄存器在 0xFEE00020，ID 在高 8 位 */
        movl $0xFEE00020, %ecx
        movl (%ecx), %eax
        shrl $24, %eax
        andl $15, %eax
        /* rsp = ap_stacks[id] */
        movq 0x7010, %rdx
        movq (%rdx,%rax,8), %rsp
        /* rdi = apic_id，然后叫 C 那边接手 */
        movl %eax, %edi
        movq 0x7008, %rcx
        callq *%rcx

1:      cli
        hlt
        jmp 1b

/* ---- GDT ------------------------------------------------------------ */

        .align 8
ap_gdt:
        .quad 0
        .quad 0x00CF9A000000FFFF        /* 0x08：32 位代码 */
        .quad 0x00CF92000000FFFF        /* 0x10：数据 */
        .quad 0x00AF9A000000FFFF        /* 0x18：64 位代码 */
ap_gdt_end:
ap_gdt_desc:
        .word ap_gdt_end - ap_gdt - 1
        .long ap_gdt
