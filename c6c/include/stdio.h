/* c6c 自带 <stdio.h>
 *
 * 这里不抄 glibc：FILE 对 c6c 就是个不透明指针，标准流用宏给出。
 * 这样运行时既能用 "fd 装进指针" 的极简实现，又不需要全局数据重定位。 */
#ifndef _C6C_STDIO_H
#define _C6C_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct __c6c_FILE FILE;

#define stdin ((FILE *)0)
#define stdout ((FILE *)1)
#define stderr ((FILE *)2)

#define EOF (-1)
#define BUFSIZ 8192
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *dst, const char *fmt, ...);
int snprintf(char *dst, size_t cap, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *dst, const char *fmt, va_list ap);
int vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap);
int puts(const char *s);
int fputs(const char *s, FILE *f);
int putchar(int c);
int getchar(void);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *buf, size_t size, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t n, FILE *f);
int fflush(FILE *f);
FILE *fdopen(int fd, const char *mode);
FILE *open_memstream(char **bufp, size_t *sizep);
FILE *freopen(const char *path, const char *mode, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
void rewind(FILE *f);
int fileno(FILE *f);
int remove(const char *path);
int rename(const char *from, const char *to);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
void setbuf(FILE *f, char *buf);
int fgetc(FILE *f);
int getc(FILE *f);
char *fgets(char *buf, int n, FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int remove(const char *path);
int rename(const char *from, const char *to);

#endif
