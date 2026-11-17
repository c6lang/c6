/* 裸 _start：不跑任何构造函数，模拟内核/裸机环境。
 *
 * 进来时 %rsp 未必是 16 字节对齐（SysV 只保证进程入口 rsp 16 对齐，
 * 而 callee 期望入口 rsp%16 == 8），所以先对齐再 call —— c6 生成的代码
 * 里有 movaps，栈歪一点就当场段错误。 */
extern int os_main(void);

__attribute__((naked)) void _start(void)
{
    __asm__ volatile (
        "andq $-16, %rsp\n\t"
        "call os_main\n\t"
        "movl %eax, %edi\n\t"
        "movl $231, %eax\n\t"      /* exit_group */
        "syscall\n\t"
        "hlt");
}
