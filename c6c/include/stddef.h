/* c6c 自带 <stddef.h> */
#ifndef _C6C_STDDEF_H
#define _C6C_STDDEF_H

typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef int wchar_t;
typedef unsigned int wint_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t) & ((type *)0)->member)

#endif
