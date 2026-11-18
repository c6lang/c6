/* c6c 自带 <stdbool.h> */
#if !defined(_C6C_STDBOOL_H) && !defined(_NOLIBC_STDBOOL_H)
#define _C6C_STDBOOL_H
/* 内核 tools/include/nolibc/stdbool.h 也定义 true/false，guard 名不同。
 * 两边都认对方的 guard：谁先被 include，另一个就整体跳过，避免同一个宏
 * 被 #define 两次（c6c 预处理器会展开 #define 的名字，二次定义
 * `#define false 0` 会变成 `#define 0 0` 报语法错）。 */
#define _NOLIBC_STDBOOL_H

#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1

#endif
