/* expect: 42 */
/* c6c 自带的 include/ 是 c6 自己的头，不该按"外部 C 头"解析。
 *
 * 回归的是这串假警告：一个只 include 自带头的 TU，c6c 曾经刷 218 行
 *   c6c/include/stddef.h:5: warning: type defaults to int
 * 根因是 in_c6_tu() 只看后缀——`.h` 在纯 C 的 TU 里被当成外部头，于是
 * `typedef unsigned long size_t;` 这类规范写法被「不带类型说明符」那条判据
 * 认成隐式 int。带 -Werror 的内核构建会被这层噪声直接淹掉。
 *
 * 这里同时用到几个自带头的符号：编得过 + 值对，就说明头是按 c6 规矩解析的
 * （真按隐式 int 走，size_t 会变成 int，下面的 sizeof 比较就会不对）。
 */
#include <stddef.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    unsigned char buf[8];
    size_t n = sizeof(size_t);
    uint32_t x = 0;

    memset(buf, 0x5a, sizeof(buf));
    x = (uint32_t)buf[3];
    /* sizeof(size_t) 在 LP64 上是 8；被当成隐式 int 的话这里会是 4 */
    if (n != 8) return 1;
    /* memchr 在自带 <string.h> 里必须有声明（stage1 自编也靠它） */
    if (memchr(buf, 0x5a, sizeof(buf)) == NULL) return 2;
    return 42;
}
