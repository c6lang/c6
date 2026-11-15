/* c6c 自带 <stdarg.h>：SysV x86-64 的 va_list 就是编译器内建的 4 字段结构。
 * 我们不抄 glibc：va_list 的形状是 ABI 的一部分，写清楚比"兼容"更重要。 */
#ifndef _C6C_STDARG_H
#define _C6C_STDARG_H

typedef struct __c6c_va_elem {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
} __c6c_va_elem;

typedef __c6c_va_elem va_list[1];
typedef __c6c_va_elem __gnuc_va_list[1];

#define va_start(ap, last) __builtin_va_start(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dst, src)  (*(dst) = *(src))
#define __va_copy(dst, src) (*(dst) = *(src))

#endif
