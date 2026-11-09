/* tests/fstr.c 的第二个翻译单元。
 *
 * 这里不出现 f".."：C99 后端在非首个翻译单元里调用"第一个实参是符号地址"
 * 的函数时会取错符号（与 f".." 无关的老问题），f".." 的文本常量正好是这种
 * 实参。所以本文件只提供"接收 f".." 结果"的一侧，用来验证拼接结果是普通
 * 指针，可以跨翻译单元传。
 */
#include <string.h>

int tu_text_len(const char *s)
{
    return (int)strlen(s);
}

/* 消息由调用方按 f".." 拼好传进来（借用的文本，跨 TU 只是普通指针） */
int!error tu_boom(int n, const char *msg)
{
    if (n)
        err(9, msg);
    return 0;
}
