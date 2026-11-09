/* 裸机 payload 的"平台"动作：就两条 outb，别的什么都没有。
 *
 * 换块板子只要改这两个函数：boot_putc 换成 UART/MMIO 写，boot_exit 换成
 * 关中断死循环（或者复位）。
 */

void boot_putc(int ch);
void boot_exit(int code);

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
