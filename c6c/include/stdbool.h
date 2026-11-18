/* c6c 自带 <stdbool.h>
 *
 * 关键：bool 类型必须**总是**定义——内核 include/linux/stddef.h 定义了
 * _LINUX_STDDEF_H 和 enum { false=0, true=1 }，却**没有**定义 bool 类型。
 * gcc 的系统 stdbool.h 与之独立，先 include linux/stddef.h 再 include
 * stdbool.h 依然有 bool；c6c 旧版把整个文件用 _LINUX_STDDEF_H guard 掉，
 * 导致 modpost 等 host tool 里 `bool x;` 变成未声明类型（type defaults to
 * int），struct 字段报"这里要 ';'"。
 *
 * 所以拆成两层：
 *   - _C6C_BOOL_H 只护 bool 类型与 __bool_true_false_are_defined，无条件生效；
 *   - true/false 与 nolibc 的 enum 互斥（谁先谁生效），避免 #define false 0
 *     把内核的 `enum { false = 0 }` 展开成 `enum { 0 = 0 }`。
 */
#ifndef _C6C_BOOL_H
#define _C6C_BOOL_H
#define bool _Bool
#define __bool_true_false_are_defined 1
#endif

#if !defined(_NOLIBC_STDBOOL_H) && !defined(_LINUX_STDDEF_H) && !defined(_C6C_TF_H)
#define _C6C_TF_H
/* 与 tools/include/nolibc/stdbool.h 同步 guard，谁先被 include 谁生效 */
#define _NOLIBC_STDBOOL_H
#define true 1
#define false 0
#endif
