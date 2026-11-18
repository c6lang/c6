/* c6c 自带 <string.h> */
#ifndef _C6C_STRING_H
#define _C6C_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
/* memchr 是 stage1 的硬依赖：src.c6 用它整段跳行（逐字节扫 1.2 MB 要 4~5 ms）。
 * 以前只在 lib/rt.c6 里有**定义**、头文件里没有**声明**，而 lib/rt.c6 是
 * 编译用户程序时才挂上去的运行时——于是 c6c 编自己（没有运行时那一层）就报
 * "未声明的标识符 'memchr'"。stage1 的构建链是
 *   tcc/c6 (C89 口径) → build/c6c → selfhost
 * 第二步正好踩在这条上，第一步因为 C89 收隐式声明而看不出来。 */
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *hay, const char *needle);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
char *strerror(int errnum);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
void *memmem(const void *hay, size_t hn, const void *needle, size_t nn);

#endif
