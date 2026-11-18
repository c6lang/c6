/* c6c 自带 <stdbool.h> */
#if !defined(_C6C_STDBOOL_H) && !defined(_NOLIBC_STDBOOL_H) && !defined(_LINUX_STDDEF_H)
#define _C6C_STDBOOL_H
/* 内核 tools/include/nolibc/stdbool.h 与 include/linux/stddef.h 都定义
 * true/false（后者是 enum { false=0, true=1 }），guard 名各异。谁先被
 * include，另一个就整体跳过，避免 c6c 把 `#define false 0` 再展开一次、
 * 或把内核的 `enum { false = 0 }` 展开成 `enum { 0 = 0 }` 报语法错。 */
#define _NOLIBC_STDBOOL_H

#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1

#endif
